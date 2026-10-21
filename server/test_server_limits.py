"""Pruebas de los límites contra saturación (v17.0). Uso: python -m unittest server/test_server_limits.py -v

Comprueba que el servidor rechaza conexiones de más (por IP y en total), salas de más y salas no permitidas
(ORAS_ROOMS), sin afectar a los que ya están jugando.
"""
import asyncio
import importlib.util
import os
import unittest

_spec = importlib.util.spec_from_file_location("oras_relay_limits", os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.py"))
server = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(server)


async def handshake(port, room=b"1234"):
    """Devuelve (reader, writer, respuesta) o (None, None, b"") si el servidor cerró la conexión."""
    r, w = await asyncio.open_connection("127.0.0.1", port)
    w.write(room)
    try:
        line = await asyncio.wait_for(r.readline(), 3)
    except (ConnectionError, asyncio.IncompleteReadError):
        line = b""
    if not line:
        w.close()
        return None, None, b""
    return r, w, line


class LimitsTest(unittest.TestCase):
    def setUp(self):
        self.saved = (server.MAX_PER_IP, server.MAX_CLIENTS, server.MAX_ROOMS, server.ALLOWED_ROOMS, server.MAX_PPS, server.BURST)
        server.rooms.clear()
        server.conn_by_ip.clear()
        server.conn_total = 0

    def tearDown(self):
        server.MAX_PER_IP, server.MAX_CLIENTS, server.MAX_ROOMS, server.ALLOWED_ROOMS, server.MAX_PPS, server.BURST = self.saved

    def run_async(self, coro):
        return asyncio.run(coro)

    async def with_server(self, body):
        srv = await asyncio.start_server(server.handle_client, "127.0.0.1", 0)
        port = srv.sockets[0].getsockname()[1]
        try:
            return await body(port)
        finally:
            srv.close()
            await srv.wait_closed()

    def test_per_ip_limit(self):
        server.MAX_PER_IP = 3

        async def body(port):
            ok = [await handshake(port) for _ in range(3)]
            self.assertTrue(all(line.startswith(b"OK:ROOM:") for _, _, line in ok))
            _, _, extra = await handshake(port)
            self.assertEqual(extra, b"")                       # la 4.ª desde la misma IP se corta
            for _, w, _ in ok:
                w.close()
            await asyncio.sleep(0.2)
            _, w, again = await handshake(port)                # al salir los demás, vuelve a entrar
            self.assertTrue(again.startswith(b"OK:ROOM:"))
            w.close()
        self.run_async(self.with_server(body))

    def test_total_limit(self):
        server.MAX_CLIENTS = 2

        async def body(port):
            a = await handshake(port, b"A")
            b = await handshake(port, b"B")
            _, _, c = await handshake(port, b"C")
            self.assertEqual(c, b"")
            for _, w, _ in (a, b):
                w.close()
        self.run_async(self.with_server(body))

    def test_room_limit(self):
        server.MAX_ROOMS = 2

        async def body(port):
            a = await handshake(port, b"sala1")
            b = await handshake(port, b"sala2")
            _, _, c = await handshake(port, b"sala3")
            self.assertEqual(c, b"ERROR:ROOM_FULL\n")
            d = await handshake(port, b"sala1")              # a una sala que ya existe sí se puede entrar
            self.assertTrue(d[2].startswith(b"OK:ROOM:sala1:"))
            for _, w, _ in (a, b, d):
                w.close()
        self.run_async(self.with_server(body))

    def test_allowed_rooms(self):
        server.ALLOWED_ROOMS = frozenset({"privada"})

        async def body(port):
            _, _, bad = await handshake(port, b"1234")
            self.assertEqual(bad, b"ERROR:ROOM_DENIED\n")
            _, w, good = await handshake(port, b"privada")
            self.assertTrue(good.startswith(b"OK:ROOM:privada:"))
            w.close()
        self.run_async(self.with_server(body))

    def test_packet_rate_limit(self):
        server.MAX_PPS, server.BURST = 20.0, 10.0

        async def body(port):
            ra, wa, la = await handshake(port, b"flood")
            rb, wb, lb = await handshake(port, b"flood")
            pid = int(la.decode().strip().split(":")[-1])
            pkt = bytes((0x01, pid)) + bytes(62)
            wb.write(bytes((0xFF, 0)) + bytes(62))            # B manda algo: ya puede recibir
            await wb.drain()
            await asyncio.sleep(0.2)
            wa.write(pkt * 200)                               # A inunda: 200 paquetes de golpe
            await wa.drain()
            got = 0
            try:
                while True:
                    await asyncio.wait_for(rb.readexactly(64), 0.5)
                    got += 1
            except (asyncio.TimeoutError, asyncio.IncompleteReadError):
                pass
            self.assertLessEqual(got, 15)                     # ráfaga (10) + lo que se recarga mientras llega
            self.assertGreaterEqual(got, 5)
            await asyncio.sleep(1.0)                          # se recargan las fichas: vuelve a pasar
            wa.write(pkt * 3)
            await wa.drain()
            again = 0
            try:
                while True:
                    await asyncio.wait_for(rb.readexactly(64), 0.5)
                    again += 1
            except (asyncio.TimeoutError, asyncio.IncompleteReadError):
                pass
            self.assertEqual(again, 3)
            wa.close()
            wb.close()
        self.run_async(self.with_server(body))


if __name__ == "__main__":
    unittest.main()
