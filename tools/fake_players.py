"""
Fake players for ORAS MMO / jugadores falsos: test the server and the plugin without a second console.
Run server/server.py locally, point the plugin to 127.0.0.1 and start some bots near your character.
  python tools/fake_players.py --zone 259 --cx 351 --cy 380 --h 2 --n 4 --variant 3 --lead 373,473,297,6
  (zone 259 = Petalburg Pokémon Center; read zone/x/y/z of your character in mmo_log.txt, "diag:" lines)

Simulador de jugadores falsos para ORAS MMO v13 (prueba el servidor y el plugin sin una 2.ª consola).

Habla el protocolo v13: handshake = código de sala en texto, respuesta "OK:ROOM:<sala>:<id>\n",
luego paquetes PlayerPacket de 64 bytes little-endian con zona en map_id (bits 16-31), yaw en lvls[0..1],
versión 13 en lvls[2], reloj del emisor (ms) en msg[0..3] y MoveState en anim.

Uso típico (bots caminando alrededor de tu personaje, misma zona):
  python fake_players.py --zone 83 --cx 999 --cy 711 --h 110 --n 3
  python fake_players.py --listen                     # solo escucha y decodifica lo que llega
  python fake_players.py --chat "hola"                # el bot 0 envía un chat al conectar
Opciones: --host (127.0.0.1) --port (9000) --room (1234) --radius 40 --speed 0.6 (rad/s) --ups 52 (u/s, 8 direcciones) --run --pause
Ctrl+C para salir.
"""
import argparse, asyncio, collections, math, random, struct, time

FMT = "<BBHIfffBBBB6s14s20s"          # == struct PlayerPacket (Protocol.hpp), 64 bytes
assert struct.calcsize(FMT) == 64
T_POS, T_EMOTE, T_CHAT, T_EVREQ, T_EVRESP, T_EVEND, T_LEAVE, T_PING = 0x01, 0x02, 0x03, 0x20, 0x21, 0x22, 0x23, 0xFF
T_STATS, T_BATTLE = 0x31, 0x32
FLAG_BATTLE = 0x01
LEADS = [252, 255, 258]
MOVE_IDLE, MOVE_WALK, MOVE_RUN = 0, 1, 2

def pack(t, pid, model=172, zone=83, x=0.0, y=0.0, h=0.0, yaw=0.0, state=0, nick="Bot", msg=b"", seq=0, stamp=None, flags=0, variant=0, acc=0):
    yaw16 = int((yaw % (2 * math.pi)) / (2 * math.pi) * 65536) & 0xFFFF
    lv = struct.pack("<HBBH", yaw16, 13, variant & 0x0F, seq & 0xFFFF)   # lvls[3]: nibble bajo = ropa (v13)
    if stamp is None:
        stamp = int(time.time() * 1000) & 0xFFFFFFFF
    if t in (T_POS, T_PING):
        msg = struct.pack("<I", stamp) + bytes(16)
    facing = 0 if math.cos(yaw) > 0.7 else 1 if math.cos(yaw) < -0.7 else (3 if math.sin(yaw) > 0 else 2)
    btype = acc if t == T_POS else 0                      # v16.5: gorro / lazo (0 como la ropa, 1 original, 2..16)
    return struct.pack(FMT, t, pid, model, (zone << 16), x, y, h, facing, state, btype, flags, lv,
                       nick.encode()[:13].ljust(14, b"\0"), msg[:20].ljust(20, b"\0"))

def unpack(b):
    t, pid, model, map_id, x, y, z, facing, anim, btype, flags, lv, nick, msg = struct.unpack(FMT, b)
    yaw16, ver, var, seq = struct.unpack("<HBBH", lv)
    return dict(type=hex(t), pid=pid, model=model, zone=map_id >> 16, x=round(x, 1), y=round(y, 1), h=round(z, 1),
                facing=facing, anim=anim, flags=flags, ver=ver, yaw=round(yaw16 / 65536 * 360), seq=seq,
                nick=nick.split(b"\0")[0].decode(errors="replace"))

async def handshake(host, port, room):
    r, w = await asyncio.open_connection(host, port)
    w.write(room.encode()); await w.drain()
    line = (await asyncio.wait_for(r.readline(), 8)).decode().strip()
    if not line.startswith("OK:ROOM:"):
        raise RuntimeError(f"handshake rechazado: {line!r}")
    return r, w, int(line.rsplit(":", 1)[1])

async def reader(r, label, verbose):
    n = 0
    while True:
        p = unpack(await r.readexactly(64)); n += 1
        if verbose or p["type"] != hex(T_POS) or n % 60 == 1:
            print(f"[{label}] <- {p}", flush=True)

async def bot(a, k):
    r, w, pid = await handshake(a.host, a.port, a.room)
    print(f"bot{k}: conectado como J{pid}", flush=True)
    asyncio.create_task(reader(r, f"bot{k}", a.verbose))
    model = 171 if k % 2 == 0 else 172
    nick = f"{a.name}{k}"[:13]
    if a.chat and k == 0:
        w.write(pack(T_CHAT, pid, model=model, zone=a.zone, nick=nick, msg=a.chat.encode()[:19])); await w.drain()
    t0 = time.time(); seq = 0
    speed = a.speed * (2.0 if a.run else 1.0)
    # Camino poligonal en 8 direcciones a velocidad real (--ups unidades/s), como un jugador con el stick.
    ups = a.ups * (2.0 if a.run and a.ups > 0 else 1.0)
    poly = []
    for i in range(8):
        an = (i + k * 3) * math.pi / 4
        poly.append((a.cx + math.cos(an) * a.radius, a.cy + math.sin(an) * a.radius))
    seg_len = [math.hypot(poly[(i + 1) % 8][0] - poly[i][0], poly[(i + 1) % 8][1] - poly[i][1]) for i in range(8)]
    total = sum(seg_len)
    dist = 0.0; last_t = 0.0; yaw = 0.0; last_stats = 0.0; last_battle = 0.0
    x, y = poly[0]
    # --jitter: cada envío se retrasa al azar 0..jitter ms (en orden, como TCP) para simular Internet con picos
    pending = collections.deque(); last_rel = 0.0
    real_write = w.write
    def jwrite(data):
        nonlocal last_rel
        if a.jitter <= 0:
            real_write(data); return
        rel = max(last_rel, time.time() + random.uniform(0, a.jitter / 1000.0))
        last_rel = rel
        pending.append((rel, data))
    w.write = jwrite
    while True:
        now_r = time.time()
        while pending and pending[0][0] <= now_r:
            real_write(pending.popleft()[1])
        t = time.time() - t0
        if a.listen:
            w.write(pack(T_PING, pid, model=model, zone=a.zone, nick=nick)); await w.drain()
            await asyncio.sleep(1.0); continue
        dt = t - last_t; last_t = t
        paused = a.pause and (t + k * 1.3) % 10 >= 7          # se detiene 3 de cada 10 s (sin saltos al volver)
        state = MOVE_IDLE if paused else (MOVE_RUN if a.run else MOVE_WALK)
        if a.bike: state = 3                                    # MOVE_BIKE (también parado, como el plugin v16)
        if a.ups > 0:
            if not paused:
                dist = (dist + ups * dt) % total
            d = dist; i = 0
            while d > seg_len[i]:
                d -= seg_len[i]; i = (i + 1) % 8
            (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % 8]
            f = d / seg_len[i]
            x, y = x0 + (x1 - x0) * f, y0 + (y1 - y0) * f
            if not paused:
                raw = math.atan2(x1 - x0, y1 - y0)               # 0 = +y (sur), +pi/2 = +x (este)
                yaw = round(raw / (math.pi / 4)) * (math.pi / 4)  # 8 direcciones como el juego
        else:
            if not paused:
                dist += speed * dt
            ang = dist + k * 2.1
            x = a.cx + math.cos(ang) * a.radius
            y = a.cy + math.sin(ang) * a.radius
            if not paused:
                yaw = math.atan2(-math.sin(ang), math.cos(ang))
        seq += 1
        fighting = a.battle and k == 0
        if fighting:
            state = MOVE_IDLE
        w.write(pack(T_POS, pid, model=model, zone=a.zone, x=x, y=y, h=a.h, yaw=yaw, state=state, nick=nick, seq=seq,
                     flags=(FLAG_BATTLE if fighting else 0) | (0x08 if a.bike and k % 2 else 0) | (0x10 if a.menu and k == 0 else 0),
                     variant=(a.variant + k) % 16 if a.variant else 0, acc=a.acc))
        if a.emote > 0 and t - getattr(a, "_last_emote_%d" % k, 0) > a.emote:
            setattr(a, "_last_emote_%d" % k, t)
            w.write(pack(0x02, pid, model=model, zone=a.zone, nick=nick, state=6))   # EMOTE 6 = Sorpresa
        # progreso para el ranking (cada 10 s)
        if t - last_stats > 10 or last_stats == 0:
            last_stats = t
            st = struct.pack("<HBBHHIBBBHB", 3 + k * 7, 20 + k * 9, (1 << (3 + k)) - 1, 30 + k * 25, 60 + k * 40,
                             15000 + k * 9000, 3 + k % 4, 18 + k * 6, 22 + k * 8, LEADS[k % len(LEADS)], 15)
            w.write(pack(T_STATS, pid, model=model, zone=a.zone, nick=nick, msg=st))
        # combate simulado: el bot 0 pelea y sus PS bajan y suben
        if fighting and t - last_battle > 0.4:
            last_battle = t
            cyc = (t % 20) / 20.0
            my_hp = int(150 * (1 - 0.5 * cyc)); op_hp = int(120 * (1 - cyc))
            bt = struct.pack("<BHBHHHBHH", 2, 257, 45, my_hp, 150, 262, 40, op_hp, 120)
            w.write(pack(T_BATTLE, pid, model=model, zone=a.zone, nick=nick, msg=bt))
        await w.drain()
        await asyncio.sleep(1 / 30)                      # ~30 Hz como el plugin v13 al moverse

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1"); ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--room", default="1234"); ap.add_argument("--n", type=int, default=1)
    ap.add_argument("--zone", type=int, default=83)
    ap.add_argument("--cx", type=float, default=999.0); ap.add_argument("--cy", type=float, default=711.0)
    ap.add_argument("--h", type=float, default=110.0); ap.add_argument("--radius", type=float, default=40.0)
    ap.add_argument("--speed", type=float, default=0.6); ap.add_argument("--run", action="store_true")
    ap.add_argument("--ups", type=float, default=0.0, help="velocidad real en unidades/s (camino en 8 direcciones); caminar ~52")
    ap.add_argument("--pause", action="store_true")
    ap.add_argument("--listen", action="store_true"); ap.add_argument("--chat", default="")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--bike", action="store_true", help="los bots van en bici (bot impar = Acro)")
    ap.add_argument("--emote", type=float, default=0.0, help="cada N s cada bot manda el emote Sorpresa")
    ap.add_argument("--jitter", type=float, default=0.0, help="ms: retraso al azar de cada envío (simula Internet con picos)")
    ap.add_argument("--name", default="Bot", help="prefijo del nombre (Bot -> Bot0, Bot1...)")
    ap.add_argument("--battle", action="store_true", help="el bot 0 simula estar en combate (panel de combate en vivo)")
    ap.add_argument("--variant", type=int, default=0, help="color de ropa 1..15 (bot k: variant+k); 0 = el original")
    ap.add_argument("--acc", type=int, default=0, help="v16.5: gorro / lazo: 0 como la ropa, 1 original, 2..16 color 1..15")
    ap.add_argument("--menu", action="store_true", help="v16.5: el bot 0 dice estar en un menú (etiqueta [menu])")
    ap.add_argument("--lead", default="252,255,258", help="especies de los Pokémon acompañantes (una por bot, en ciclo)")
    a = ap.parse_args()
    global LEADS
    LEADS = [int(x) for x in a.lead.split(",")]
    await asyncio.gather(*(bot(a, k) for k in range(a.n)))

if __name__ == "__main__":
    try: asyncio.run(main())
    except KeyboardInterrupt: pass
