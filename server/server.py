"""
server.py — Project ORAS MMO Relay Server (TCP Ultra-Fast) v13
==============================================================
Servidor de sincronización multijugador para Pokémon ORAS (Crossplatform PC & Android).
Retransmite paquetes binarios de 64 bytes en tiempo real con latencia mínima (<5ms).

Puerto TCP: 9000 (configurable con variable de entorno PORT)

Protocolo (sin cambios respecto a v12.6):
  1. El cliente envía el código de sala en ASCII (sin terminador).
  2. El servidor responde "OK:ROOM:<sala>:<id>\n" (id 1..N) o "ERROR:ROOM_FULL\n".
  3. Después ambos lados intercambian paquetes fijos de 64 bytes (struct PlayerPacket).

v16: guarda las estadísticas (paquete STATS 0x31) de cada jugador por sala en ranking_<sala>.json y, al
entrar un cliente v16, le envía el ranking completo (también de los desconectados) con paquetes 0x33.
v16.3: quien se cambia el nombre ya no sale dos veces (se borra su entrada vieja) y el nombre de fábrica "Jugador" no
se guarda (ni al cargar archivos viejos).

v17.0: límites contra saturación (muchas conexiones o salas falsas): conexiones en total y por IP, salas abiertas a
la vez y salas con ranking guardado. Opcional: ORAS_ROOMS = lista de salas permitidas (servidor privado).
v17.0 r3: como mucho 4 jugadores de una misma IP en una sala, 5 códigos de sala distintos por IP cada 10 min (misma
respuesta que una sala no permitida) y solo se reenvían los tipos de paquete que manda el plugin (lista blanca). Este
mismo equipo (127.x) no cuenta para esos límites, para poder probar con bots locales.
v17.0 r4: ranking con llave. El plugin manda (solo al servidor, paquete 0x34) una huella de su llave secreta
mmo_key.txt; el primero que guarda puntos con un nombre se queda con él. Otro jugador con ese nombre (o un plugin viejo
sin llave) no puede cambiar ni borrar esos puntos, los demás lo ven como "nombre*" y él recibe NAME_TAKEN (0x35). Un
nombre sin jugar ORAS_RANK_CLAIM_DAYS días se puede volver a usar, y cada IP crea como mucho ORAS_RANK_NEW_PER_IP
nombres nuevos en el ranking al día. El "OK:ROOM:<sala>:KEY:<id>" avisa al plugin de que el servidor entiende la llave.

Variables de entorno opcionales:
  PORT (9000) · ORAS_MAX_PLAYERS (12) · ORAS_IDLE_TIMEOUT (60 s) · ORAS_QUEUE_MAX (256 paquetes)
  ORAS_RANK_DIR (carpeta del servidor) · ORAS_RANK_SAVE (30 s)
  ORAS_MAX_CLIENTS (300) · ORAS_MAX_PER_IP (16) · ORAS_MAX_ROOMS (100) · ORAS_RANK_MAX_ROOMS (200)
  ORAS_MAX_PPS (120 paquetes/s por jugador; el plugin manda ~30) · ORAS_BURST (240)
  ORAS_ROOMS ("" = cualquier sala; p. ej. "MiSala,Otra" = solo esas)
  ORAS_MAX_PER_IP_ROOM (4) · ORAS_ROOM_TRIES (5) · ORAS_ROOM_TRIES_WINDOW (600 s)
  ORAS_RANK_CLAIM_DAYS (30) · ORAS_RANK_NEW_PER_IP (5 nombres nuevos al día)
"""

import asyncio
import collections
import hashlib
import json
import logging
import os
import socket
import struct
import sys
import time
from typing import Dict, List, Optional

# Consolas de Windows en cp1252 con salida redirigida: nunca reventar por un emoji.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(errors="replace")
    except Exception:
        pass

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("oras_server")

PORT_TCP = int(os.environ.get("PORT", 9000))
MAX_PLAYERS = int(os.environ.get("ORAS_MAX_PLAYERS", 12))       # jugadores por sala
IDLE_TIMEOUT = float(os.environ.get("ORAS_IDLE_TIMEOUT", 60))   # s sin un paquete completo -> expulsar
QUEUE_MAX = int(os.environ.get("ORAS_QUEUE_MAX", 256))          # paquetes pendientes por cliente
HANDSHAKE_TIMEOUT = 8.0
STATUS_INTERVAL = 60.0
PKT_SIZE = 64
MAX_ROOM_CODE = 16

PKT_POSITION, PKT_LEAVE, PKT_PING = 0x01, 0x23, 0xFF
PKT_STATS, PKT_RANK_SNAPSHOT = 0x31, 0x33
RANK_MIN_VERSION = 16           # byte 26 (lvls[2]) = versión de protocolo del cliente
RANK_DIR = os.environ.get("ORAS_RANK_DIR", os.path.dirname(os.path.abspath(__file__)))
RANK_SAVE_INTERVAL = float(os.environ.get("ORAS_RANK_SAVE", 30))
RANK_MAX_ENTRIES = 64           # por sala; al pasarse se borra la más antigua
MAX_CLIENTS = int(os.environ.get("ORAS_MAX_CLIENTS", 300))      # conexiones abiertas en total
MAX_PER_IP = int(os.environ.get("ORAS_MAX_PER_IP", 16))         # conexiones desde una misma IP (más que una sala llena)
MAX_ROOMS = int(os.environ.get("ORAS_MAX_ROOMS", 100))          # salas abiertas a la vez
RANK_MAX_ROOMS = int(os.environ.get("ORAS_RANK_MAX_ROOMS", 200))  # salas distintas con ranking en disco
MAX_PPS = float(os.environ.get("ORAS_MAX_PPS", 120))           # paquetes por segundo de un jugador (media)
BURST = float(os.environ.get("ORAS_BURST", 240))                # ráfaga permitida
ALLOWED_ROOMS = frozenset(r.strip() for r in os.environ.get("ORAS_ROOMS", "").split(",") if r.strip())
# v17.0 r3 (auditoría de seguridad): una sola IP ya no puede llenar una sala, ni probar muchos códigos de sala
MAX_PER_IP_ROOM = int(os.environ.get("ORAS_MAX_PER_IP_ROOM", 4))           # jugadores de una misma IP en una sala
ROOM_TRIES = int(os.environ.get("ORAS_ROOM_TRIES", 5))                     # códigos de sala distintos por IP...
ROOM_TRIES_WINDOW = float(os.environ.get("ORAS_ROOM_TRIES_WINDOW", 600))   # ...en esta ventana (s)
EXEMPT_LOOPBACK = True          # este mismo equipo (pruebas, bots locales) no cuenta para esos dos límites
# Solo se reenvían los tipos que manda el plugin. LEAVE (0x23) y RANK_SNAPSHOT (0x33) los genera solo el servidor:
# si llegan de un cliente se descartan (salidas o ranking falsos para los demás).
RELAY_TYPES = frozenset((PKT_POSITION, 0x02, 0x03, 0x20, 0x21, 0x22, PKT_PING, PKT_STATS, 0x32))
# v17.0 r4: ranking con llave. KEY (cliente -> servidor, nunca se reenvía): msg[0..15] = huella de la llave del jugador
# para este servidor y sala. NAME_TAKEN (servidor -> ese cliente): su nombre ya es de otro jugador.
PKT_KEY, PKT_NAME_TAKEN = 0x34, 0x35
KEY_LEN = 16
RANK_CLAIM_DAYS = float(os.environ.get("ORAS_RANK_CLAIM_DAYS", 30))   # nombre sin jugar tantos días: se puede reclamar
RANK_NEW_PER_IP = int(os.environ.get("ORAS_RANK_NEW_PER_IP", 5))     # nombres nuevos en el ranking por IP...
RANK_NEW_WINDOW = 86400.0                                             # ...cada 24 h (nadie llena el ranking de falsos)
OFF_MODEL, OFF_FLAGS, OFF_VERSION, OFF_NAME, OFF_MSG = 2, 23, 26, 30, 44
STATS_LEN = 18
DROPPABLE = (PKT_POSITION, PKT_PING)    # "último valor gana": se pueden descartar si un cliente va lento


class Client:
    """Un jugador conectado. Tiene su propia cola de salida y su propia tarea escritora,
    así un cliente lento (combate, app en 2.º plano, Wi-Fi mala) nunca frena a los demás."""

    def __init__(self, pid: int, room_code: str, writer: asyncio.StreamWriter, ip: str = ""):
        self.pid = pid
        self.room_code = room_code
        self.writer = writer
        self.ip = ip            # v17.0 r3: para el límite de jugadores por IP en una sala
        self.bad_types = 0      # paquetes de tipos que no se reenvían
        self.queue = collections.deque()
        self.wake = asyncio.Event()
        self.ready = False      # recibe retransmisiones solo tras enviar su 1.er paquete (ya leyó el OK)
        self.closed = False
        self.dropped = 0
        self.task = None        # type: Optional[asyncio.Future]
        self.version = 0
        self.name = ""
        self.model = 0
        self.tokens = BURST     # v17.0: límite de paquetes por segundo (cubo de fichas)
        self.last_tick = time.monotonic()
        self.flooded = 0        # paquetes descartados por pasarse del límite
        self.key = None         # type: Optional[str]  # v17.0 r4: huella de su llave (None = plugin sin llave)
        self.denied = False     # usa un nombre que es de otro: no entra en el ranking y los demás lo ven con "*"
        self.denied_for = ""    # nombre por el que ya se le avisó (NAME_TAKEN una vez por nombre)
        self.wrote = set()      # nombres (rank_key) cuyo ranking guardó en esta conexión
        self.new_warned = False

    def allow(self) -> bool:
        """v17.0: un jugador no puede inundar la sala (cada paquete se reenvía a todos los demás)."""
        now = time.monotonic()
        self.tokens = min(BURST, self.tokens + (now - self.last_tick) * MAX_PPS)
        self.last_tick = now
        if self.tokens < 1.0:
            self.flooded += 1
            return False
        self.tokens -= 1.0
        return True

    def send(self, pkt: bytes) -> None:
        """Encola sin bloquear. Si la cola está llena descarta la POSITION/PING más antigua."""
        if self.closed:
            return
        q = self.queue
        if len(q) >= QUEUE_MAX:
            for i, old in enumerate(q):
                if old[0] in DROPPABLE:
                    del q[i]
                    self.dropped += 1
                    break
            else:
                log.warning(f"⚠️ Jugador {self.pid} (sala '{self.room_code}') no lee sus paquetes: cola llena, desconectado.")
                self.kill()
                return
        q.append(pkt)
        self.wake.set()

    async def writer_loop(self) -> None:
        try:
            while not self.closed:
                if not self.queue:
                    self.wake.clear()
                    await self.wake.wait()
                    continue
                data = b"".join(self.queue)
                self.queue.clear()
                self.writer.write(data)
                await self.writer.drain()
        except Exception:           # (CancelledError es BaseException desde 3.8: no entra aquí)
            self.kill()

    def kill(self) -> None:
        """Corta la conexión de inmediato (el lector recibe EOF y hace la limpieza)."""
        self.closed = True
        self.wake.set()
        try:
            self.writer.transport.abort()
        except Exception:
            pass


# room_code -> {player_id: Client}
rooms: Dict[str, Dict[int, Client]] = {}
# v17.0: conexiones abiertas (en total y por IP), para los límites contra saturación
conn_total = 0
conn_by_ip: Dict[str, int] = {}
# v17.0 r3: códigos de sala probados por cada IP (código -> última vez) y último aviso en el registro por IP
room_tries: Dict[str, Dict[str, float]] = {}
tries_warned: Dict[str, float] = {}
# v17.0 r4: cuándo creó cada IP un nombre nuevo en el ranking (límite diario)
rank_new: Dict[str, List[float]] = {}


def is_loopback(ip: str) -> bool:
    return EXEMPT_LOOPBACK and (ip.startswith("127.") or ip in ("::1", "::ffff:127.0.0.1"))


def room_try_allowed(ip: str, code: str, now: float) -> bool:
    """v17.0 r3: una IP puede probar como mucho ROOM_TRIES códigos de sala distintos cada ROOM_TRIES_WINDOW s (así
    no se puede ir probando códigos hasta dar con la sala de otro grupo). Volver a la misma sala no cuenta."""
    if is_loopback(ip):
        return True
    tried = room_tries.setdefault(ip, {})
    for old in [c for c, t in tried.items() if now - t > ROOM_TRIES_WINDOW]:
        del tried[old]
    if code not in tried and len(tried) >= ROOM_TRIES:
        return False
    tried[code] = now
    return True


def prune_room_tries(now: float) -> None:
    """Olvida las IP que ya no tienen intentos dentro de la ventana (para que la tabla no crezca sin fin)."""
    for ip in [i for i, tried in room_tries.items() if all(now - t > ROOM_TRIES_WINDOW for t in tried.values())]:
        del room_tries[ip]
    for ip in [i for i, t in tries_warned.items() if now - t > ROOM_TRIES_WINDOW]:
        del tries_warned[ip]
    for ip in [i for i, ts in rank_new.items() if all(now - t > RANK_NEW_WINDOW for t in ts)]:
        del rank_new[ip]


def new_name_allowed(ip: str, now: float) -> bool:
    """v17.0 r4: una IP crea como mucho RANK_NEW_PER_IP nombres nuevos en el ranking al día (si no, cambiando de nombre
    sin parar se llenaría el ranking de falsos y se borrarían las entradas de verdad, que son como mucho 64)."""
    if is_loopback(ip):
        return True
    recent = [t for t in rank_new.get(ip, []) if now - t < RANK_NEW_WINDOW]
    if len(recent) >= RANK_NEW_PER_IP:
        rank_new[ip] = recent
        return False
    recent.append(now)
    rank_new[ip] = recent
    return True


def key_fingerprint(token: bytes) -> str:
    """v17.0 r4: lo que se guarda es la huella de la llave, nunca la llave (así el archivo del ranking no la revela)."""
    return hashlib.sha256(b"oras-rank-key|" + token).hexdigest()


def is_fingerprint(v) -> bool:
    return isinstance(v, str) and len(v) == 64 and all(c in "0123456789abcdef" for c in v)


def shown_name(name: str) -> str:
    """Cómo ven los demás a quien usa un nombre que es de otro jugador: con un "*" (no se confunde con el dueño)."""
    return name[:12] + "*"


def with_name(pkt: bytes, name: str) -> bytes:
    raw = name.encode("ascii", "replace")[:13].ljust(14, b"\x00")
    return pkt[:OFF_NAME] + raw + pkt[OFF_NAME + 14:]


def name_taken_packet(pid: int, name: str) -> bytes:
    p = bytearray(PKT_SIZE)
    p[0] = PKT_NAME_TAKEN
    p[1] = pid
    p[OFF_VERSION] = RANK_MIN_VERSION
    return with_name(bytes(p), name)


def packet_name(pkt: bytes) -> str:
    raw = pkt[OFF_NAME:OFF_NAME + 14].split(b"\x00", 1)[0]
    return "".join(chr(c) for c in raw if 0x20 <= c <= 0x7E).strip()


def rank_key(name: str) -> str:
    """Una persona = un nombre (sin mayúsculas). v16.1: el modelo ya no cuenta, porque el cliente manda
    "chico" hasta que detecta el género y la misma persona salía dos veces."""
    return name.strip().lower()


# v16.3: el nombre de fábrica del plugin (quien aún no puso el suyo) no se guarda en el ranking: si luego se pone su
# nombre, salía dos veces ("Jugador (desc.)" y su nombre, con los mismos puntos)
DEFAULT_NAMES = frozenset({"jugador"})


def rankable(name: str) -> bool:
    k = rank_key(name)
    return bool(k) and k not in DEFAULT_NAMES


class StatsBook:
    """Última estadística conocida de cada jugador (clave: nombre en minúsculas), por sala.
    Se guarda en disco de forma atómica para sobrevivir a reinicios del servidor."""

    def __init__(self, folder: str):
        self.folder = folder
        self.rooms: Dict[str, Dict[str, dict]] = {}
        self.dirty = set()
        os.makedirs(folder, exist_ok=True)
        for fn in os.listdir(folder):
            if not (fn.startswith("ranking_") and fn.endswith(".json")):
                continue
            try:
                room = bytes.fromhex(fn[8:-5]).decode("ascii")
                with open(os.path.join(folder, fn), encoding="utf-8") as f:
                    data = json.load(f)
                book: Dict[str, dict] = {}
                for v in data.values():
                    if not (isinstance(v, dict) and isinstance(v.get("name"), str)
                            and len(bytes.fromhex(v.get("stats", ""))) == STATS_LEN):
                        continue
                    k = rank_key(v["name"])          # archivos de la v16 ("nombre|modelo"): se unen, gana el más reciente
                    if not rankable(v["name"]):      # v16.3: "Jugador" (nombre de fábrica) fuera
                        continue
                    if "owner" in v and not is_fingerprint(v["owner"]):
                        v.pop("owner")               # v17.0 r4: huella rota = entrada sin dueño
                    if k not in book or v.get("last_seen", 0) > book[k].get("last_seen", 0):
                        book[k] = v
                self.rooms[room] = book
                if len(book) != len(data):
                    self.dirty.add(room)
            except Exception as e:
                log.warning(f"⚠️ Ranking ilegible {fn}: {e!r} (se ignora)")

    def _path(self, room: str) -> str:
        return os.path.join(self.folder, f"ranking_{room.encode('ascii').hex()}.json")   # hex: nunca sale de la carpeta

    def has(self, room: str, name: str) -> bool:
        return rank_key(name) in self.rooms.get(room, {})

    def owner_ok(self, room: str, name: str, key: Optional[str], now: Optional[float] = None) -> bool:
        """v17.0 r4: ¿puede quien tiene la llave `key` (None = plugin sin llave) usar ese nombre en el ranking? Sí si el
        nombre es nuevo, no tiene dueño o es suyo; también si su dueño lleva RANK_CLAIM_DAYS días sin jugar."""
        if not rankable(name):
            return True
        e = self.rooms.get(room, {}).get(rank_key(name))
        if e is None or not e.get("owner") or e["owner"] == key:
            return True
        now = time.time() if now is None else now
        return now - e.get("last_seen", 0) > RANK_CLAIM_DAYS * 86400

    def owned_by(self, room: str, name: str, key: Optional[str]) -> bool:
        e = self.rooms.get(room, {}).get(rank_key(name))
        return key is not None and e is not None and e.get("owner") == key

    def update(self, room: str, name: str, model: int, stats: bytes, now: Optional[float] = None,
               owner: Optional[str] = None) -> bool:
        """Guarda sus puntos. v17.0 r4: con llave (owner), el nombre queda a su nombre; sin llave, la entrada queda sin
        dueño (plugins viejos). Nunca pisa la entrada de otro dueño. True = guardado."""
        if not rankable(name) or len(stats) != STATS_LEN:
            return False
        if room not in self.rooms and len(self.rooms) >= RANK_MAX_ROOMS:
            return False                             # v17.0: tope de salas con ranking (nadie llena el disco)
        now = time.time() if now is None else now
        if not self.owner_ok(room, name, owner, now):
            return False
        book = self.rooms.setdefault(room, {})
        entry = {"name": name, "model": model, "stats": stats.hex(), "last_seen": now}
        if owner:
            entry["owner"] = owner
        book[rank_key(name)] = entry
        while len(book) > RANK_MAX_ENTRIES:
            del book[min(book, key=lambda k: book[k]["last_seen"])]
        self.dirty.add(room)
        return True

    def forget(self, room: str, name: str) -> None:
        """v16.3: la persona se cambió el nombre: su entrada con el nombre viejo sobra."""
        book = self.rooms.get(room)
        if book is not None and book.pop(rank_key(name), None) is not None:
            self.dirty.add(room)

    def entries(self, room: str) -> List[dict]:
        return list(self.rooms.get(room, {}).values())

    def save_all(self) -> None:
        for room in list(self.dirty):
            path = self._path(room)
            tmp = path + ".tmp"
            try:
                with open(tmp, "w", encoding="utf-8") as f:
                    json.dump(self.rooms.get(room, {}), f, ensure_ascii=False)
                os.replace(tmp, path)
                self.dirty.discard(room)
            except OSError as e:
                log.warning(f"⚠️ No se pudo guardar el ranking de '{room}': {e!r}")


BOOK = StatsBook(RANK_DIR)


def send_rank_snapshot(room: Dict[int, Client], client: Client) -> None:
    """Todas las entradas del ranking de la sala, marcando quién está desconectado."""
    online = {rank_key(c.name): pid for pid, c in room.items() if c.name and not c.denied}   # r4: no quien usa un nombre ajeno
    now = time.time()
    for e in BOOK.entries(client.room_code):
        pid = online.get(rank_key(e["name"]), 0)
        p = bytearray(PKT_SIZE)
        p[0] = PKT_RANK_SNAPSHOT
        p[1] = pid
        struct.pack_into("<H", p, OFF_MODEL, e["model"] & 0xFFFF)
        p[OFF_FLAGS] = 0 if pid else 0x80
        p[OFF_VERSION] = RANK_MIN_VERSION
        name = e["name"].encode("ascii", "replace")[:13]
        p[OFF_NAME:OFF_NAME + len(name)] = name
        p[OFF_MSG:OFF_MSG + STATS_LEN] = bytes.fromhex(e["stats"])
        days = int(max(0.0, now - e["last_seen"]) // 86400)
        struct.pack_into("<H", p, OFF_MSG + STATS_LEN, min(days, 0xFFFF))
        client.send(bytes(p))


def parse_room_code(data: bytes) -> Optional[str]:
    code = data.strip(b"\x00 \t\r\n\x0b\x0c")
    if not 1 <= len(code) <= MAX_ROOM_CODE or any(c < 0x20 or c > 0x7E for c in code):
        return None
    return code.decode("ascii")


def relay(room: Dict[int, Client], sender: int, pkt: bytes) -> None:
    for pid, c in list(room.items()):
        if pid != sender and c.ready:
            c.send(pkt)


def tune_socket(writer: asyncio.StreamWriter) -> None:
    """TCP_NODELAY + keepalive (mejor esfuerzo, detecta conexiones muertas a nivel de SO)."""
    sock = writer.get_extra_info("socket")
    if sock is None:
        return
    opts = [(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1), (socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)]
    opts += [(socket.IPPROTO_TCP, getattr(socket, n), v)        # Linux/macOS
             for n, v in (("TCP_KEEPIDLE", 15), ("TCP_KEEPINTVL", 5), ("TCP_KEEPCNT", 4)) if hasattr(socket, n)]
    for level, opt, val in opts:
        try:
            sock.setsockopt(level, opt, val)
        except Exception:
            pass
    if hasattr(socket, "SIO_KEEPALIVE_VALS"):        # Windows: (activado, inactividad ms, intervalo ms)
        try:
            raw = socket.socket(fileno=sock.fileno())  # envoltorio temporal sin duplicar el handle
            try:
                raw.ioctl(socket.SIO_KEEPALIVE_VALS, (1, 15000, 5000))
            finally:
                raw.detach()
        except Exception:
            pass
    try:
        writer.transport.set_write_buffer_limits(high=16384)   # pocos datos viejos atascados en el SO
    except Exception:
        pass


async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    global conn_total
    addr = writer.get_extra_info('peername') or ("?",)
    ip = str(addr[0])
    room_code = None
    client = None
    if conn_total >= MAX_CLIENTS or conn_by_ip.get(ip, 0) >= MAX_PER_IP:
        log.warning(f"⛔ Conexión rechazada de {ip}: límite de conexiones ({conn_total} abiertas, {conn_by_ip.get(ip, 0)} de esa IP)")
        try:
            writer.transport.abort()
        except Exception:
            pass
        return
    conn_total += 1
    conn_by_ip[ip] = conn_by_ip.get(ip, 0) + 1
    try:
        tune_socket(writer)
        try:
            data = await asyncio.wait_for(reader.read(256), timeout=HANDSHAKE_TIMEOUT)
        except asyncio.TimeoutError:
            log.warning(f"⚠️ Timeout esperando handshake de sala ({addr[0]})")
            return
        room_code = parse_room_code(data)
        if room_code is None:
            if data:
                log.warning(f"⚠️ Código de sala inválido desde {addr[0]}: {data[:32]!r}")
            return
        # v17.0 r3: misma respuesta que una sala no permitida (no dice si la sala existe)
        now = time.monotonic()
        if not room_try_allowed(ip, room_code, now):
            if ip not in tries_warned:
                tries_warned[ip] = now
                log.warning(f"⛔ {ip} prueba demasiados códigos de sala distintos (más de {ROOM_TRIES} en {ROOM_TRIES_WINDOW:.0f} s): rechazada")
            writer.write(b"ERROR:ROOM_DENIED\n")
            await asyncio.wait_for(writer.drain(), timeout=5.0)
            room_code = None
            return
        if ALLOWED_ROOMS and room_code not in ALLOWED_ROOMS:
            log.warning(f"⛔ Sala no permitida '{room_code}' desde {addr[0]}")
            writer.write(b"ERROR:ROOM_DENIED\n")
            await asyncio.wait_for(writer.drain(), timeout=5.0)
            room_code = None
            return
        if room_code not in rooms and len(rooms) >= MAX_ROOMS:
            log.warning(f"⛔ Demasiadas salas abiertas ({len(rooms)}): rechazada '{room_code}' de {addr[0]}")
            writer.write(b"ERROR:ROOM_FULL\n")
            await asyncio.wait_for(writer.drain(), timeout=5.0)
            room_code = None
            return

        room = rooms.setdefault(room_code, {})
        player_id = next((i for i in range(1, MAX_PLAYERS + 1) if i not in room), -1)
        if player_id == -1:
            log.warning(f"Sala '{room_code}' llena (máx {MAX_PLAYERS} jugadores). Rechazando IP: {addr[0]}")
            writer.write(b"ERROR:ROOM_FULL\n")
            await asyncio.wait_for(writer.drain(), timeout=5.0)
            return
        # v17.0 r3: una sola IP no puede ocupar toda la sala (para el plugin es "sala llena": reintenta en 15 s)
        if not is_loopback(ip) and sum(1 for c in room.values() if c.ip == ip) >= MAX_PER_IP_ROOM:
            log.warning(f"⛔ Sala '{room_code}': ya hay {MAX_PER_IP_ROOM} jugadores desde {ip}. Rechazado")
            writer.write(b"ERROR:ROOM_FULL\n")
            await asyncio.wait_for(writer.drain(), timeout=5.0)
            return

        client = Client(player_id, room_code, writer, ip)
        room[player_id] = client
        log.info(f"✅ Jugador {player_id} conectado a la sala '{room_code}' desde {addr[0]} ({len(room)}/{MAX_PLAYERS})")

        # Respuesta de bienvenida: OK:ROOM:<code>:KEY:<player_id>  (va sola: aún no es receptor). v17.0 r4: "KEY:" = este
        # servidor entiende la llave del ranking; los plugins viejos solo leen lo que va tras el último ':' (el id).
        writer.write(f"OK:ROOM:{room_code}:KEY:{player_id}\n".encode('ascii'))
        client.task = asyncio.ensure_future(client.writer_loop())

        # Bucle principal de retransmisión de paquetes (64 bytes exactos)
        id_byte = bytes((player_id & 0xFF,))
        while True:
            try:
                packet = await asyncio.wait_for(reader.readexactly(PKT_SIZE), timeout=IDLE_TIMEOUT)
            except asyncio.TimeoutError:
                log.warning(f"⏱️ Jugador {player_id} sin actividad {IDLE_TIMEOUT:.0f}s en sala '{room_code}' ({addr[0]}). Liberando su lugar.")
                return
            if not client.allow():                    # v17.0: demasiados paquetes por segundo: se descartan
                if client.flooded in (1, 1000):
                    log.warning(f"⛔ Jugador {player_id} (sala '{room_code}', {addr[0]}) manda demasiados paquetes: se descartan")
                continue
            if packet[0] == PKT_KEY:                  # v17.0 r4: su llave del ranking (solo la ve el servidor)
                token = packet[OFF_MSG:OFF_MSG + KEY_LEN]
                if client.key is None and any(token):  # una por conexión: no se pueden probar llaves en bucle
                    client.key = key_fingerprint(token)
                continue
            if packet[0] not in RELAY_TYPES:          # v17.0 r3: lista blanca de tipos (ni ranking ni salidas falsos)
                client.bad_types += 1
                if client.bad_types in (1, 1000):
                    log.warning(f"⛔ Jugador {player_id} (sala '{room_code}', {addr[0]}) manda paquetes de tipo 0x{packet[0]:02X}: se descartan")
                continue
            if packet[1] != player_id:                # anti-suplantación
                packet = packet[:1] + id_byte + packet[2:]
            first = not client.ready
            client.ready = True
            client.version = packet[OFF_VERSION]
            nm = packet_name(packet)
            if nm:
                if client.name and rank_key(nm) != rank_key(client.name):   # v16.3: se cambió el nombre
                    log.info(f"✏️ Jugador {player_id} ahora se llama '{nm}' (antes '{client.name}')")
                    # v17.0 r4: su entrada vieja se borra solo si era suya (su llave, o la guardó él en esta conexión)
                    if BOOK.owned_by(room_code, client.name, client.key) or (
                            rank_key(client.name) in client.wrote and BOOK.owner_ok(room_code, client.name, client.key)):
                        BOOK.forget(room_code, client.name)
                client.name = nm
                client.model = struct.unpack_from("<H", packet, OFF_MODEL)[0]
            if client.name:                           # v17.0 r4: ¿ese nombre es de otro jugador?
                client.denied = not BOOK.owner_ok(room_code, client.name, client.key)
                if not client.denied:
                    client.denied_for = ""
                elif client.denied_for != rank_key(client.name):
                    client.denied_for = rank_key(client.name)
                    log.info(f"🔒 Jugador {player_id} ('{client.name}', {addr[0]}) usa un nombre que es de otro jugador: "
                             f"fuera del ranking")
                    if client.key is not None:        # los plugins con llave entienden el aviso (los viejos no)
                        client.send(name_taken_packet(player_id, client.name))
            if packet[0] == PKT_STATS and client.name and not client.denied and packet[OFF_MSG + 17] & 0x0F:   # sin datos conocidos: no pisa lo guardado
                if rankable(client.name) and (BOOK.has(room_code, client.name) or new_name_allowed(ip, time.monotonic())):
                    if BOOK.update(room_code, client.name, client.model, packet[OFF_MSG:OFF_MSG + STATS_LEN], owner=client.key):
                        client.wrote.add(rank_key(client.name))
                elif rankable(client.name) and not client.new_warned:
                    client.new_warned = True
                    log.warning(f"⛔ {ip} ya creó {RANK_NEW_PER_IP} nombres nuevos en el ranking hoy: '{client.name}' no se guarda")
            if first and client.version >= RANK_MIN_VERSION:
                send_rank_snapshot(room, client)
            if client.denied:                         # v17.0 r4: sus puntos no entran y los demás lo ven como "nombre*"
                if packet[0] == PKT_STATS:
                    continue
                packet = with_name(packet, shown_name(client.name))
            # Susurro: CHAT (0x03) con btype (byte 22) = id del destinatario -> solo a esa persona
            if packet[0] == 0x03 and packet[22] != 0:
                target = room.get(packet[22])
                if target is not None and target.ready and packet[22] != player_id:
                    target.send(packet)
                continue
            relay(room, player_id, packet)

    except (asyncio.IncompleteReadError, ConnectionError, OSError):
        if client is not None:
            log.info(f"❌ Jugador {client.pid} desconectado de sala '{room_code}' ({addr[0]})")
    except Exception as e:
        log.debug(f"Desconexión de cliente ({addr[0]}): {e!r}")
    finally:
        conn_total -= 1
        left = conn_by_ip.get(ip, 1) - 1
        if left > 0:
            conn_by_ip[ip] = left
        else:
            conn_by_ip.pop(ip, None)
        if client is not None:
            client.closed = True
            client.wake.set()
            if client.task is not None:
                client.task.cancel()
            room = rooms.get(room_code)
            if room is not None and room.get(client.pid) is client:
                del room[client.pid]
            if client.dropped:
                log.info(f"   (J{client.pid}: {client.dropped} posiciones viejas descartadas por conexión lenta)")
            if client.flooded:
                log.info(f"   (J{client.pid}: {client.flooded} paquetes descartados por pasarse de {MAX_PPS:.0f}/s)")
            if room:
                relay(room, client.pid, bytes((PKT_LEAVE, client.pid)) + bytes(PKT_SIZE - 2))
            elif room is not None:
                del rooms[room_code]
                log.info(f"Sala '{room_code}' cerrada (sin jugadores).")
        try:
            if writer.transport.get_write_buffer_size():
                writer.transport.abort()               # no esperar a un cliente que no lee
            else:
                writer.close()
            await asyncio.wait_for(writer.wait_closed(), timeout=2.0)
        except Exception:
            pass


async def status_loop():
    while True:
        await asyncio.sleep(STATUS_INTERVAL)
        prune_room_tries(time.monotonic())
        players = sum(len(r) for r in rooms.values())
        detail = ", ".join(f"'{k}':{len(v)}" for k, v in sorted(rooms.items()))
        log.info(f"📊 Estado: {len(rooms)} sala(s), {players} jugador(es) conectados" + (f" [{detail}]" if detail else ""))


async def rank_save_loop():
    while True:
        await asyncio.sleep(RANK_SAVE_INTERVAL)
        BOOK.save_all()


async def main():
    tcp_server = await asyncio.start_server(handle_client, '0.0.0.0', PORT_TCP)
    log.info("==================================================")
    log.info(f"🚀 Servidor ORAS MMO ACTIVO en el puerto {PORT_TCP} (TCP)")
    log.info("   Crossplay: Azahar PC y Azahar Android")
    log.info(f"   Máximo {MAX_PLAYERS} jugadores por sala · inactividad {IDLE_TIMEOUT:.0f}s")
    log.info(f"   Por IP: {MAX_PER_IP} conexiones, {MAX_PER_IP_ROOM} en una misma sala, {ROOM_TRIES} códigos de sala cada {ROOM_TRIES_WINDOW:.0f}s")
    log.info(f"   Salas permitidas: {', '.join(sorted(ALLOWED_ROOMS)) if ALLOWED_ROOMS else 'cualquiera'}")
    log.info(f"   Ranking con llave: un nombre sin jugar {RANK_CLAIM_DAYS:.0f} días se libera; {RANK_NEW_PER_IP} nombres nuevos por IP al día")
    log.info("   Esperando conexiones de jugadores...")
    log.info("==================================================")
    status = asyncio.ensure_future(status_loop())
    saver = asyncio.ensure_future(rank_save_loop())
    try:
        async with tcp_server:
            await tcp_server.serve_forever()
    finally:
        status.cancel()
        saver.cancel()
        BOOK.save_all()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("Servidor detenido (Ctrl+C).")
