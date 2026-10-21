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

Variables de entorno opcionales:
  PORT (9000) · ORAS_MAX_PLAYERS (12) · ORAS_IDLE_TIMEOUT (60 s) · ORAS_QUEUE_MAX (256 paquetes)
  ORAS_RANK_DIR (carpeta del servidor) · ORAS_RANK_SAVE (30 s)
  ORAS_MAX_CLIENTS (300) · ORAS_MAX_PER_IP (16) · ORAS_MAX_ROOMS (100) · ORAS_RANK_MAX_ROOMS (200)
  ORAS_MAX_PPS (120 paquetes/s por jugador; el plugin manda ~30) · ORAS_BURST (240)
  ORAS_ROOMS ("" = cualquier sala; p. ej. "MiSala,Otra" = solo esas)
"""

import asyncio
import collections
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
OFF_MODEL, OFF_FLAGS, OFF_VERSION, OFF_NAME, OFF_MSG = 2, 23, 26, 30, 44
STATS_LEN = 18
DROPPABLE = (PKT_POSITION, PKT_PING)    # "último valor gana": se pueden descartar si un cliente va lento


class Client:
    """Un jugador conectado. Tiene su propia cola de salida y su propia tarea escritora,
    así un cliente lento (combate, app en 2.º plano, Wi-Fi mala) nunca frena a los demás."""

    def __init__(self, pid: int, room_code: str, writer: asyncio.StreamWriter):
        self.pid = pid
        self.room_code = room_code
        self.writer = writer
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
                    if k not in book or v.get("last_seen", 0) > book[k].get("last_seen", 0):
                        book[k] = v
                self.rooms[room] = book
                if len(book) != len(data):
                    self.dirty.add(room)
            except Exception as e:
                log.warning(f"⚠️ Ranking ilegible {fn}: {e!r} (se ignora)")

    def _path(self, room: str) -> str:
        return os.path.join(self.folder, f"ranking_{room.encode('ascii').hex()}.json")   # hex: nunca sale de la carpeta

    def update(self, room: str, name: str, model: int, stats: bytes, now: Optional[float] = None) -> None:
        if not rankable(name) or len(stats) != STATS_LEN:
            return
        if room not in self.rooms and len(self.rooms) >= RANK_MAX_ROOMS:
            return                                   # v17.0: tope de salas con ranking (nadie llena el disco)
        book = self.rooms.setdefault(room, {})
        book[rank_key(name)] = {"name": name, "model": model, "stats": stats.hex(),
                                "last_seen": time.time() if now is None else now}
        while len(book) > RANK_MAX_ENTRIES:
            del book[min(book, key=lambda k: book[k]["last_seen"])]
        self.dirty.add(room)

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
    online = {rank_key(c.name): pid for pid, c in room.items() if c.name}
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

        client = Client(player_id, room_code, writer)
        room[player_id] = client
        log.info(f"✅ Jugador {player_id} conectado a la sala '{room_code}' desde {addr[0]} ({len(room)}/{MAX_PLAYERS})")

        # Respuesta de bienvenida: OK:ROOM:<code>:<player_id>  (va sola: aún no es receptor)
        writer.write(f"OK:ROOM:{room_code}:{player_id}\n".encode('ascii'))
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
            if packet[1] != player_id:                # anti-suplantación
                packet = packet[:1] + id_byte + packet[2:]
            first = not client.ready
            client.ready = True
            client.version = packet[OFF_VERSION]
            nm = packet_name(packet)
            if nm:
                if client.name and rank_key(nm) != rank_key(client.name):   # v16.3: se cambió el nombre
                    log.info(f"✏️ Jugador {player_id} ahora se llama '{nm}' (antes '{client.name}')")
                    BOOK.forget(room_code, client.name)
                client.name = nm
                client.model = struct.unpack_from("<H", packet, OFF_MODEL)[0]
            if packet[0] == PKT_STATS and client.name and packet[OFF_MSG + 17] & 0x0F:   # sin datos conocidos: no pisa lo guardado
                BOOK.update(room_code, client.name, client.model, packet[OFF_MSG:OFF_MSG + STATS_LEN])
            if first and client.version >= RANK_MIN_VERSION:
                send_rank_snapshot(room, client)
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
