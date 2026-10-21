"""Pruebas del ranking persistente (v16). Uso: python -m unittest server/test_server_ranking.py -v"""
import asyncio
import json
import os
import struct
import sys
import tempfile
import unittest

import importlib.util  # noqa: E402

# Cargado por ruta: "server" también es el nombre de la carpeta (paquete) desde la raíz del repo
_spec = importlib.util.spec_from_file_location("oras_relay", os.path.join(os.path.dirname(os.path.abspath(__file__)), "server.py"))
server = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(server)

PKT = 64


def packet(ptype, pid=1, version=16, name=b"ana", model=172, msg=b"", flags=0):
    p = bytearray(PKT)
    p[0] = ptype
    p[1] = pid
    struct.pack_into("<H", p, 2, model)
    p[23] = flags
    p[26] = version
    p[30:30 + len(name)] = name
    p[44:44 + len(msg)] = msg
    return bytes(p)


def stats_msg(badges=0xFF, caught=258):
    m = bytearray(18)
    m[3] = badges
    struct.pack_into("<H", m, 4, caught)
    m[17] = 0x0F
    return bytes(m)


class Peer:
    def __init__(self, reader, writer, pid):
        self.r, self.w, self.pid = reader, writer, pid

    @classmethod
    async def join(cls, port, room=b"1234"):
        r, w = await asyncio.open_connection("127.0.0.1", port)
        w.write(room)
        line = await asyncio.wait_for(r.readline(), 3)
        pid = int(line.decode().strip().split(":")[-1])
        return cls(r, w, pid)

    async def send(self, pkt):
        self.w.write(pkt[:1] + bytes((self.pid,)) + pkt[2:])
        await self.w.drain()

    async def recv_all(self, timeout=0.4):
        out = []
        while True:
            try:
                out.append(await asyncio.wait_for(self.r.readexactly(PKT), timeout))
            except (asyncio.TimeoutError, asyncio.IncompleteReadError):
                return out

    def close(self):
        self.w.close()


class RankingTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        server.rooms.clear()
        server.BOOK = server.StatsBook(self.tmp.name)
        self.srv = await asyncio.start_server(server.handle_client, "127.0.0.1", 0)
        self.port = self.srv.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.srv.close()
        await self.srv.wait_closed()
        self.tmp.cleanup()

    async def test_stats_persist_across_restart(self):
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, msg=stats_msg()))
        await asyncio.sleep(0.2)
        server.BOOK.save_all()
        book2 = server.StatsBook(self.tmp.name)
        entries = book2.entries("1234")
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["name"], "ana")
        self.assertEqual(bytes.fromhex(entries[0]["stats"])[3], 0xFF)
        a.close()

    async def test_new_client_gets_snapshot(self):
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, msg=stats_msg()))
        await asyncio.sleep(0.2)
        a.close()
        await asyncio.sleep(0.2)
        b = await Peer.join(self.port)
        await b.send(packet(server.PKT_PING, name=b"beto", version=16))
        got = [p for p in await b.recv_all() if p[0] == server.PKT_RANK_SNAPSHOT]
        self.assertEqual(len(got), 1)
        snap = got[0]
        self.assertEqual(snap[30:33], b"ana")
        self.assertEqual(snap[44 + 3], 0xFF)            # medallas
        self.assertTrue(snap[23] & 0x80)                # desconectada
        self.assertEqual(snap[1], 0)
        b.close()

    async def test_old_client_gets_no_snapshot(self):
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, msg=stats_msg()))
        await asyncio.sleep(0.2)
        old = await Peer.join(self.port)
        await old.send(packet(server.PKT_PING, name=b"viejo", version=15))
        got = await old.recv_all()
        self.assertFalse(any(p[0] == server.PKT_RANK_SNAPSHOT for p in got))
        a.close(); old.close()

    async def test_same_name_different_model_is_one_person(self):
        # v16.1: el cliente manda "chico" (172) hasta detectar el género: era la misma persona dos veces
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, name=b"ana", model=172, msg=stats_msg(0x01)))
        await a.send(packet(server.PKT_STATS, name=b"Ana", model=171, msg=stats_msg(0x03)))
        await asyncio.sleep(0.2)
        entries = server.BOOK.entries("1234")
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["model"], 171)                  # el último que mandó
        self.assertEqual(bytes.fromhex(entries[0]["stats"])[3], 0x03)
        a.close()

    async def test_load_merges_old_duplicates(self):
        # ranking guardado por la v16 con la misma persona dos veces: al cargar queda una (la más reciente)
        old = {"rowan|172": {"name": "Rowan", "model": 172, "stats": stats_msg(0x01).hex(), "last_seen": 100.0},
               "rowan|171": {"name": "Rowan", "model": 171, "stats": stats_msg(0x07).hex(), "last_seen": 200.0},
               "sage|172": {"name": "Sage", "model": 172, "stats": stats_msg(0x0F).hex(), "last_seen": 150.0}}
        with open(os.path.join(self.tmp.name, "ranking_" + b"1234".hex() + ".json"), "w", encoding="utf-8") as f:
            json.dump(old, f)
        book = server.StatsBook(self.tmp.name)
        entries = sorted(book.entries("1234"), key=lambda e: e["name"])
        self.assertEqual([e["name"] for e in entries], ["Rowan", "Sage"])
        self.assertEqual(entries[0]["model"], 171)
        self.assertEqual(bytes.fromhex(entries[0]["stats"])[3], 0x07)

    async def test_snapshot_online_by_name(self):
        # guardado como 172, conectado ahora como 171: sale como conectado (no "desc.")
        server.BOOK.update("1234", "ana", 172, stats_msg(), now=1)
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_PING, name=b"ana", model=171, version=16))
        await asyncio.sleep(0.1)
        b = await Peer.join(self.port)
        await b.send(packet(server.PKT_PING, name=b"beto", version=16))
        got = [p for p in await b.recv_all() if p[0] == server.PKT_RANK_SNAPSHOT and p[30:33] == b"ana"]
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0][1], a.pid)
        self.assertFalse(got[0][23] & 0x80)
        a.close(); b.close()

    async def test_unknown_stats_do_not_overwrite(self):
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, msg=stats_msg(0xFF)))
        await asyncio.sleep(0.1)
        empty = bytearray(stats_msg(0)); empty[17] = 0          # arrancando: nada conocido
        await a.send(packet(server.PKT_STATS, msg=bytes(empty)))
        await asyncio.sleep(0.2)
        e = server.BOOK.entries("1234")[0]
        self.assertEqual(bytes.fromhex(e["stats"])[3], 0xFF)
        a.close()

    async def test_book_cap_64(self):
        for i in range(70):
            server.BOOK.update("1234", f"j{i}", 172, stats_msg(), now=1000 + i)
        entries = server.BOOK.entries("1234")
        self.assertEqual(len(entries), 64)
        self.assertNotIn("j0", [e["name"] for e in entries])   # se borra la más antigua
        self.assertIn("j69", [e["name"] for e in entries])

    async def test_rename_replaces_old_entry(self):
        # v16.3: la misma conexión cambia de nombre -> una sola entrada (antes salía dos veces con los mismos puntos)
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, name=b"ana", msg=stats_msg(0x03)))
        await asyncio.sleep(0.1)
        await a.send(packet(server.PKT_STATS, name=b"anita", msg=stats_msg(0x03)))
        await asyncio.sleep(0.2)
        self.assertEqual([e["name"] for e in server.BOOK.entries("1234")], ["anita"])
        a.close()

    async def test_default_name_is_not_stored(self):
        # entró sin nombre ("Jugador", el de fábrica) y luego puso el suyo: en el ranking solo sale con el suyo
        a = await Peer.join(self.port)
        await a.send(packet(server.PKT_STATS, name=b"Jugador", msg=stats_msg()))
        await asyncio.sleep(0.1)
        self.assertEqual(server.BOOK.entries("1234"), [])
        await a.send(packet(server.PKT_STATS, name=b"Maple", msg=stats_msg()))
        await asyncio.sleep(0.2)
        self.assertEqual([e["name"] for e in server.BOOK.entries("1234")], ["Maple"])
        b = await Peer.join(self.port)
        await b.send(packet(server.PKT_PING, name=b"beto", version=16))
        names = [p[30:44].split(b"\0")[0] for p in await b.recv_all() if p[0] == server.PKT_RANK_SNAPSHOT]
        self.assertEqual(names, [b"Maple"])
        a.close(); b.close()

    async def test_load_drops_default_name(self):
        old = {"jugador": {"name": "Jugador", "model": 172, "stats": stats_msg(0x0F).hex(), "last_seen": 100.0},
               "maple": {"name": "Maple", "model": 172, "stats": stats_msg(0x0F).hex(), "last_seen": 100.0}}
        with open(os.path.join(self.tmp.name, "ranking_" + b"1234".hex() + ".json"), "w", encoding="utf-8") as f:
            json.dump(old, f)
        book = server.StatsBook(self.tmp.name)
        self.assertEqual([e["name"] for e in book.entries("1234")], ["Maple"])
        book.save_all()                                   # y el archivo queda limpio
        with open(os.path.join(self.tmp.name, "ranking_" + b"1234".hex() + ".json"), encoding="utf-8") as f:
            self.assertEqual(list(json.load(f)), ["maple"])

    async def test_room_code_cannot_escape_dir(self):
        server.BOOK.update("../x", "ana", 172, stats_msg(), now=1)
        server.BOOK.save_all()
        for f in os.listdir(self.tmp.name):
            self.assertTrue(os.path.realpath(os.path.join(self.tmp.name, f)).startswith(os.path.realpath(self.tmp.name)))
            json.load(open(os.path.join(self.tmp.name, f), encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
