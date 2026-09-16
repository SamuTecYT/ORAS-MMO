"""
pc_client.py — Project ORAS MMO PC Memory Client
=================================================
Automates the connection between Azahar (Citra) and the Relay Server using GDB Stub.
Features:
- Connects to Azahar's built-in GDB stub (Port 24689)
- Reads Player X, Y, Map ID, and Battle State safely
- Connects to WebSocket server and syncs position
"""

import os
import sys
import time
import struct
import asyncio
import socket
import websockets

# ORAS v1.4 3DS FCRAM Offsets
OFFSET_PLAYER_X    = 0x08C6E894
OFFSET_PLAYER_Y    = 0x08C6E89C
OFFSET_MAP_ID      = 0x08C6E884
OFFSET_FACING      = 0x08C6E886
OFFSET_BATTLE_PTR  = 0x081FB478

# Packet types
PKT_POSITION = 0x01
PKT_BATTLE_START = 0x02
PKT_BATTLE_END = 0x03

class GDBClient:
    def __init__(self, host='127.0.0.1', port=24689):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(2.0)
            self.sock.connect((self.host, self.port))
            # Ack any pending output
            self.sock.send(b'+')
            return True
        except Exception:
            return False

    def send_cmd(self, cmd):
        checksum = sum(cmd.encode('ascii')) % 256
        packet = f"${cmd}#{checksum:02x}".encode('ascii')
        self.sock.send(packet)
        try:
            # Wait for +
            while True:
                c = self.sock.recv(1)
                if c == b'+':
                    break
                if not c:
                    return None
            res = b""
            while True:
                c = self.sock.recv(1)
                if not c:
                    break
                if c == b'$':
                    continue
                if c == b'#':
                    self.sock.recv(2) # read checksum
                    self.sock.send(b'+') # Ack the response!
                    break
                res += c
            return res.decode('ascii')
        except socket.timeout:
            return None

    def read_mem(self, addr, length):
        res = self.send_cmd(f"m{addr:X},{length:X}")
        if not res or res.startswith('E'):
            return b'\x00' * length
        try:
            return bytes.fromhex(res)
        except ValueError:
            return b'\x00' * length

    def read_float(self, addr):
        data = self.read_mem(addr, 4)
        if len(data) < 4:
            return 0.0
        return struct.unpack('<f', data)[0]

    def read_u32(self, addr):
        data = self.read_mem(addr, 4)
        if len(data) < 4:
            return 0
        return struct.unpack('<I', data)[0]

    def read_u16(self, addr):
        data = self.read_mem(addr, 2)
        if len(data) < 2:
            return 0
        return struct.unpack('<H', data)[0]

async def mmo_client(ip: str, room_code: str):
    gdb = GDBClient()
    print("[*] Waiting for Azahar GDB Stub (Port 24689)...")
    print("[!] ASEGÚRATE de tener el GDB Stub activado en Azahar y el juego corriendo.")
    while not gdb.connect():
        await asyncio.sleep(1)
        
    print("[+] Conectado al emulador Azahar! Conectando al servidor Relay...")
    
    uri = f"ws://{ip}:8765/"
    
    try:
        async with websockets.connect(uri) as ws:
            # --- Handshake ---
            await ws.send(room_code)
            resp = await ws.recv()
            if not isinstance(resp, str) or not resp.startswith("OK:"):
                print(f"[-] Error al unirse a la sala: {resp}")
                return
            
            parts = resp.split(":")
            my_player_id = int(parts[2])
            print(f"[+] Conectado al Servidor MMO! Eres el Jugador {my_player_id}")
            
            last_x, last_y, last_map = 0.0, 0.0, 0
            in_battle = False
            
            players_data = {}
            import json
            
            while True:
                # 1. Read local player state
                px = gdb.read_float(OFFSET_PLAYER_X)
                py = gdb.read_float(OFFSET_PLAYER_Y)
                map_id = gdb.read_u16(OFFSET_MAP_ID)
                facing = gdb.read_u16(OFFSET_FACING) & 0xFF
                
                players_data[str(my_player_id)] = {"x": px, "y": py, "map": map_id, "facing": facing, "is_me": True}
                
                battle_state_ptr = gdb.read_u32(OFFSET_BATTLE_PTR)
                is_battling = False
                if battle_state_ptr != 0:
                    state_val = gdb.read_u32(battle_state_ptr)
                    if state_val == 0x40001:
                        is_battling = True

                # 2. Check for state changes
                if is_battling and not in_battle:
                    in_battle = True
                    pkt = struct.pack("<BBBBIfffBBBB6s34s", PKT_BATTLE_START, my_player_id, 171, 0, map_id, px, py, 0.0, facing, 0, 0, 0, b'\x00'*6, b'\x00'*34)
                    await ws.send(pkt)
                elif not is_battling and in_battle:
                    in_battle = False
                    pkt = struct.pack("<BBBBIfffBBBB6s34s", PKT_BATTLE_END, my_player_id, 171, 0, map_id, px, py, 0.0, facing, 0, 0, 0, b'\x00'*6, b'\x00'*34)
                    await ws.send(pkt)

                # 3. Send Position Update if moved
                if px != last_x or py != last_y or map_id != last_map:
                    pkt = struct.pack("<BBBBIfffBBBB6s34s", PKT_POSITION, my_player_id, 171, 0, map_id, px, py, 0.0, facing, 0, 0, 0, b'\x00'*6, b'\x00'*34)
                    await ws.send(pkt)
                    last_x, last_y, last_map = px, py, map_id
                    
                # 4. Receive other players' packets
                try:
                    data = await asyncio.wait_for(ws.recv(), timeout=1/20.0)
                    if len(data) == 64:
                        pkt_type, pid, loc, _, mid, rx, ry, rz, rface, anim, btype, flags, lvls, pad = struct.unpack("<BBBBIfffBBBB6s34s", data)
                        if pkt_type == PKT_POSITION:
                            players_data[str(pid)] = {"x": rx, "y": ry, "map": mid, "facing": rface, "is_me": False}
                        elif pkt_type == 0x10: # Disconnect
                            if str(pid) in players_data:
                                del players_data[str(pid)]
                except asyncio.TimeoutError:
                    pass
                
                # Write to JSON for Radar
                try:
                    with open("radar_data.json", "w") as f:
                        json.dump(players_data, f)
                except:
                    pass
                    
    except Exception as e:
        print(f"[-] Error de conexión: {e}")

if __name__ == "__main__":
    print("Project ORAS MMO - Client PC")
    print("============================")
    ip_addr = input("Ingresa la IP del Servidor (deja en blanco si eres el Host): ")
    if ip_addr.strip() == "":
        ip_addr = "127.0.0.1"
    room = input("Ingresa el código de la sala (ej. 1234): ")
    asyncio.run(mmo_client(ip_addr, room))
