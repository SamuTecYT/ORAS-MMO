"""
Pruebas de regresión del relé ORAS MMO. Carga server.py como módulo (no lo modifica) y lo levanta
en puertos altos de 127.0.0.1 (19300-19399). Imprime PASS/FAIL por prueba.

  python server/regression_server.py [server/server.py]      (también en la CI de GitHub)

Antes de importar fija ORAS_IDLE_TIMEOUT=3 (para que ghost_slot no tarde 60 s) y ORAS_MAX_PLAYERS=12.
Por eso todo cliente de prueba que deba sobrevivir más de 3 s envía algo periódicamente.

Pruebas:
 1. handshake_framing: el plugin hace recv(255) para leer "OK:ROOM:..". Si en ese recv llegan también
    paquetes de otros jugadores, esos bytes se pierden y el flujo de 64 B queda desalineado para siempre.
 2. slow_reader_isolation: un cliente que no lee (combate, app en 2.º plano) pero sigue enviando PING NO debe
    congelar a los demás (y no se le expulsa: solo se le descartan posiciones viejas).
 3. ghost_slot: clientes mudos (Wi-Fi caído) liberan su slot tras ORAS_IDLE_TIMEOUT.
 4. room_capacity: 12 clientes -> ids 1..12; el 13.º recibe ERROR:ROOM_FULL; se reutiliza el id libre más bajo.
 5. player_id_overwrite: el byte 1 retransmitido es siempre el id real del emisor.
 6. leave_broadcast: al salir un jugador, los demás reciben tipo 0x23 con su id y el resto en cero.
 7. legacy_client_alignment: cliente que imita al plugin v12.6 (recv(255) único + tramas de 64 B) nunca ve
    una trama desalineada mientras otros 11 envían a 30 Hz durante 15 s (se reconecta 5 veces).
 8. throughput_12x30hz: 12 clientes a 30 Hz durante 20 s; cada uno recibe >= 95 % y latencia mediana < 20 ms.
"""
import asyncio, importlib.util, logging, os, socket, statistics, struct, sys, tempfile, time

IDLE = 3.0
os.environ["ORAS_IDLE_TIMEOUT"] = str(IDLE)
os.environ["ORAS_MAX_PLAYERS"] = "12"
os.environ.setdefault("ORAS_RANK_DIR", tempfile.mkdtemp(prefix="oras_regr_"))   # nunca toca un ranking real
MAGIC = b"SPAM"

def load(path):
    spec = importlib.util.spec_from_file_location("oras_server_under_test", path)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    logging.disable(logging.CRITICAL); return m

def pkt(pid, i, t=1, seq=0, ts=0.0, nick=b""):
    b = bytearray(64); b[0] = t; b[1] = pid; struct.pack_into("<f", b, 8, float(i))
    struct.pack_into("<I", b, 24, seq); b[30:30 + len(nick)] = nick; struct.pack_into("<d", b, 44, ts)
    return bytes(b)

async def join(port, room):
    r, w = await asyncio.open_connection("127.0.0.1", port)
    w.write(room); await w.drain(); line = await r.readline()
    return r, w, int(line.decode().strip().rsplit(":", 1)[1])

async def join_ready(port, room):
    """Une y envía el 1.er paquete (PING); a partir de ahí el servidor le retransmite."""
    r, w, pid = await join(port, room)
    w.write(pkt(pid, 0, t=0xFF)); await w.drain(); return r, w, pid

async def pinger(w, pid, every=0.5):
    while True:
        w.write(pkt(pid, 0, t=0xFF)); await w.drain(); await asyncio.sleep(every)

async def serve(S, port):
    return await asyncio.start_server(S.handle_client, "127.0.0.1", port)

async def t_handshake(S, port):
    srv = await serve(S, port)
    ra, wa, _ = await join_ready(port, b"H")
    stop = False
    async def spam():
        i = 0
        while not stop:
            wa.write(pkt(1, i)); await wa.drain(); i += 1; await asyncio.sleep(0.002)
    t = asyncio.create_task(spam()); bad = 0; N = 20
    loop = asyncio.get_running_loop()
    for _ in range(N):
        def plugin_like():
            s = socket.create_connection(("127.0.0.1", port)); s.sendall(b"H"); time.sleep(0.03)
            d = s.recv(255); s.close(); return d
        d = await loop.run_in_executor(None, plugin_like)
        if d.partition(b"\n")[2]: bad += 1
    stop = True; await t; wa.close(); srv.close()
    return bad == 0, f"{bad}/{N} handshakes recibieron bytes de juego pegados al OK"

async def t_slow(S, port):
    srv = await serve(S, port); loop = asyncio.get_running_loop()
    ra, wa, pa = await join_ready(port, b"S")
    def b_join():                                     # B: receptor real que jamás lee
        s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        s.connect(("127.0.0.1", port)); s.sendall(b"S"); resp = s.recv(255)
        s.sendall(pkt(2, 0, t=0xFF)); return s, resp
    sb, resp_b = await loop.run_in_executor(None, b_join)
    rc, wc, pc = await join_ready(port, b"S")
    got = sent = 0
    async def rd():
        nonlocal got
        while True: await rc.readexactly(64); got += 1
    async def sd():
        nonlocal sent
        for i in range(10**7):
            wa.write(pkt(pa, i)); sent += 1; await wa.drain()
            if i % 50 == 0: await asyncio.sleep(0)
    async def b_ping():                               # B sigue vivo (PING) pero no lee nada
        while True:
            await loop.run_in_executor(None, sb.sendall, pkt(2, 0, t=0xFF)); await asyncio.sleep(0.5)
    tasks = [asyncio.create_task(c) for c in (rd(), sd(), b_ping(), pinger(wc, pc))]
    await asyncio.sleep(1.5); g1 = got; await asyncio.sleep(1.5); g2 = got
    for t in tasks: t.cancel()
    b = S.rooms.get("S", {}).get(2); b_alive = b is not None and not b.closed
    dropped = b.dropped if b else "?"
    sb.close(); wa.close(); wc.close(); srv.close()
    return g2 > g1 + 100 and b_alive and resp_b.partition(b"\n")[0].rsplit(b":", 1)[-1] == b"2", (   # r4: OK:ROOM:S:KEY:2
        f"C recibió {g1} -> {g2} en 1.5 s más (A envió {sent}); B (no lee) sigue conectado={b_alive}, "
        f"posiciones viejas descartadas para B={dropped}")

async def t_ghost(S, port):
    srv = await serve(S, port)
    keep = [await join(port, b"G") for _ in range(12)]     # sala llena de conexiones mudas
    r, w = await asyncio.open_connection("127.0.0.1", port); w.write(b"G"); await w.drain()
    full = (await r.readline()).decode().strip(); w.close()
    await asyncio.sleep(IDLE + 1.5)
    r, w = await asyncio.open_connection("127.0.0.1", port); w.write(b"G"); await w.drain()
    line = (await r.readline()).decode().strip(); w.close(); srv.close()
    for _, kw, _ in keep: kw.close()
    return full == "ERROR:ROOM_FULL" and line.startswith("OK"), \
        f"llena={full!r}; tras {IDLE + 1.5:.1f}s de silencio (timeout {IDLE:.0f}s) nuevo jugador recibe: {line!r}"

async def t_capacity(S, port):
    srv = await serve(S, port)
    conns = [await join(port, b"CAP") for _ in range(12)]
    ids = [c[2] for c in conns]
    r, w = await asyncio.open_connection("127.0.0.1", port); w.write(b"CAP"); await w.drain()
    line = await r.readline(); eof = (await asyncio.wait_for(r.read(10), 3)) == b""
    conns[4][1].close(); await asyncio.sleep(0.3)                  # libera el id 5 -> lo reutiliza
    _, w2, pid = await join(port, b"CAP")
    for c in conns: c[1].close()
    w2.close(); srv.close()
    ok = ids == list(range(1, 13)) and line == b"ERROR:ROOM_FULL\n" and eof and pid == 5
    return ok, f"ids={ids}; 13.º -> {line!r} (cierra={eof}); tras salir J5 el nuevo recibe id {pid}"

async def t_overwrite(S, port):
    srv = await serve(S, port)
    ra, wa, pa = await join_ready(port, b"OW")
    rb, wb, pb = await join_ready(port, b"OW")
    wa.write(pkt(7, 1, t=3)); wa.write(pkt(200, 2, t=1)); wa.write(pkt(pa, 3, t=2)); await wa.drain()
    got = [await asyncio.wait_for(rb.readexactly(64), 3) for _ in range(3)]
    wa.close(); wb.close(); srv.close()
    ids = [g[1] for g in got]; types = [g[0] for g in got]
    return ids == [pa] * 3 and types == [3, 1, 2], f"A es J{pa}; B recibió player_id={ids} tipos={types} (enviados 7/200/{pa})"

async def t_leave(S, port):
    srv = await serve(S, port)
    ra, wa, pa = await join_ready(port, b"LV")
    rb, wb, pb = await join_ready(port, b"LV")
    await asyncio.sleep(0.1); wa.close()
    while True:
        f = await asyncio.wait_for(rb.readexactly(64), 3)
        if f[0] == 0x23: break
    wb.close(); srv.close()
    return f == bytes((0x23, pa)) + bytes(62), f"B recibió LEAVE type=0x{f[0]:02X} pid={f[1]} (J{pa} salió) resto_cero={not any(f[2:])}"

def legacy_session(port, room, seconds, stats):
    """Exactamente lo que hace v12.6: send(code); recv(255) una vez; luego send 64 B cada 33 ms y
    recv no bloqueante acumulando en un búfer y cortando tramas de 64 B."""
    s = socket.create_connection(("127.0.0.1", port)); s.sendall(room)
    resp = s.recv(255)
    if not resp.startswith(b"OK:ROOM:"):
        s.close(); stats["rejected"] += 1; return
    pid = int(resp.split(b"\n")[0].rsplit(b":", 1)[1]); stats["extra"] += len(resp.partition(b"\n")[2])
    s.setblocking(False); buf = b""; t_end = time.time() + seconds; i = 0
    while time.time() < t_end:
        s.sendall(pkt(pid, i, nick=b"Legacy")); i += 1
        while True:
            try: d = s.recv(4096)
            except BlockingIOError: break
            if not d: raise RuntimeError("servidor cerró")
            buf += d
        while len(buf) >= 64:
            f, buf = buf[:64], buf[64:]; stats["frames"] += 1
            valid = ((f[0] == 1 and f[30:34] == MAGIC and 1 <= f[1] <= 12)
                     or (f[0] in (0x23, 0xFF) and 1 <= f[1] <= 12 and not any(f[2:])))
            if not valid: stats["bad"] += 1
        time.sleep(0.033)
    s.close(); stats["sessions"] += 1

async def spammer(port, room, seconds):
    r, w, pid = await join_ready(port, room)
    async def drain_rx():
        while True:
            if not await r.read(65536): return
    rx = asyncio.create_task(drain_rx()); t_end = time.time() + seconds; i = 0
    while time.time() < t_end:
        w.write(pkt(pid, i, nick=MAGIC)); await w.drain(); i += 1; await asyncio.sleep(1 / 30)
    rx.cancel(); w.close()

async def t_legacy(S, port):
    srv = await serve(S, port)
    sp = [asyncio.create_task(spammer(port, b"LEG", 15)) for _ in range(11)]
    await asyncio.sleep(1.0)
    stats = dict(frames=0, bad=0, extra=0, sessions=0, rejected=0)
    def run():
        for _ in range(5):
            legacy_session(port, b"LEG", 2.4, stats); time.sleep(0.3)
    await asyncio.get_running_loop().run_in_executor(None, run)
    await asyncio.gather(*sp); srv.close()
    ok = stats["bad"] == 0 and stats["extra"] == 0 and stats["sessions"] == 5 and stats["frames"] > 3000
    return ok, (f"{stats['sessions']} sesiones legacy, {stats['frames']} tramas, desalineadas={stats['bad']}, "
                f"bytes pegados al OK={stats['extra']}, rechazos={stats['rejected']}")

async def t_throughput(S, port, n=12, seconds=20.0):
    srv = await serve(S, port)
    conns = [await join_ready(port, b"TP") for _ in range(n)]
    sent = {c[2]: 0 for c in conns}; recv = {c[2]: 0 for c in conns}; lat = []
    async def reader(r, me):
        while True:
            f = await r.readexactly(64)
            if f[0] == 1 and f[30:34] == MAGIC:
                recv[me] += 1; lat.append(time.perf_counter() - struct.unpack_from("<d", f, 44)[0])
    async def sender(w, me):
        t_next = time.perf_counter(); t_end = t_next + seconds
        while time.perf_counter() < t_end:
            w.write(pkt(me, 0, seq=sent[me], ts=time.perf_counter(), nick=MAGIC)); sent[me] += 1
            await w.drain(); t_next += 1 / 30; await asyncio.sleep(max(0, t_next - time.perf_counter()))
    await asyncio.sleep(0.3)
    rds = [asyncio.create_task(reader(r, p)) for r, w, p in conns]
    await asyncio.gather(*(sender(w, p) for r, w, p in conns))
    await asyncio.sleep(1.0)
    for t in rds: t.cancel()
    for c in conns: c[1].close()
    srv.close()
    total = sum(sent.values())
    ratios = {p: recv[p] / (total - sent[p]) for p in sent}
    med = statistics.median(lat) * 1000; p99 = sorted(lat)[int(len(lat) * 0.99)] * 1000
    ok = min(ratios.values()) >= 0.95 and med < 20
    return ok, (f"{n} clientes x {total / n:.0f} paquetes; recibido mín {min(ratios.values()) * 100:.1f}% "
                f"/ máx {max(ratios.values()) * 100:.1f}%; latencia mediana {med:.2f} ms, p99 {p99:.2f} ms")

async def main(path):
    S = load(path); ok_all = True
    for name, fn, port in [("handshake_framing", t_handshake, 19301),
                           ("slow_reader_isolation", t_slow, 19302),
                           ("ghost_slot", t_ghost, 19303),
                           ("room_capacity", t_capacity, 19304),
                           ("player_id_overwrite", t_overwrite, 19305),
                           ("leave_broadcast", t_leave, 19306),
                           ("legacy_client_alignment", t_legacy, 19307),
                           ("throughput_12x30hz", t_throughput, 19308)]:
        try: ok, info = await asyncio.wait_for(fn(S, port), 60)
        except Exception as e: ok, info = False, f"excepción {e!r}"
        ok_all &= ok; print(f"{'PASS' if ok else 'FAIL'}  {name}: {info}", flush=True)
        await asyncio.sleep(0.5)
    return 0 if ok_all else 1

if __name__ == "__main__":
    sys.exit(asyncio.run(main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.py"))))
