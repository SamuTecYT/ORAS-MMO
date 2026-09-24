"""
server.py — Project ORAS MMO Relay Server (TCP Ultra-Fast)
==========================================================
Servidor de sincronización multijugador para Pokémon ORAS (Crossplatform PC & Android).
Retransmite paquetes binarios de 64 bytes en tiempo real con latencia mínima (<5ms).

Puerto TCP: 9000 (configurable con variable de entorno PORT)
"""

import asyncio
import logging
import os
from typing import Dict

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("oras_server")

PORT_TCP = int(os.environ.get("PORT", 9000))

# room_code -> dict of {player_id: {'reader': reader, 'writer': writer}}
rooms: Dict[str, Dict[int, Dict]] = {}

def get_next_player_id(room_clients):
    for i in range(1, 7):
        if i not in room_clients:
            return i
    return -1

async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    addr = writer.get_extra_info('peername')
    room_code = None
    player_id = -1
    
    try:
        data = await asyncio.wait_for(reader.read(256), timeout=8.0)
        room_code = data.decode('utf-8', errors='ignore').strip('\x00').strip()
        if not room_code:
            raise ValueError("Código de sala vacío")
            
        if room_code not in rooms:
            rooms[room_code] = {}
            
        player_id = get_next_player_id(rooms[room_code])
        if player_id == -1:
            log.warning(f"Sala '{room_code}' llena (máx 6 jugadores). Rechazando IP: {addr[0]}")
            writer.write(b"ERROR:ROOM_FULL\n")
            await writer.drain()
            return
            
        rooms[room_code][player_id] = {'reader': reader, 'writer': writer}
        log.info(f"✅ Jugador {player_id} conectado a la sala '{room_code}' desde {addr[0]}")
        
        # Respuesta de bienvenida: OK:ROOM:<code>:<player_id>
        resp = f"OK:ROOM:{room_code}:{player_id}\n".encode('utf-8')
        writer.write(resp)
        await writer.drain()
        
        # Bucle principal de retransmisión de paquetes (64 bytes exactos)
        while True:
            packet = await reader.readexactly(64)
            for pid, info in list(rooms[room_code].items()):
                if pid != player_id:
                    try:
                        w = info['writer']
                        w.write(packet)
                        await w.drain()
                    except Exception:
                        pass
                        
    except (asyncio.IncompleteReadError, ConnectionResetError):
        log.info(f"❌ Jugador {player_id if player_id != -1 else '?'} desconectado de sala '{room_code}' ({addr[0]})")
    except asyncio.TimeoutError:
        log.warning(f"⚠️ Timeout esperando handshake de sala ({addr[0]})")
    except Exception as e:
        log.debug(f"Desconexión de cliente ({addr[0]}): {e}")
    finally:
        if room_code and room_code in rooms:
            if player_id in rooms[room_code]:
                del rooms[room_code][player_id]
            if not rooms[room_code]:
                del rooms[room_code]
                log.info(f"Sala '{room_code}' cerrada (sin jugadores).")
                
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

async def main():
    tcp_server = await asyncio.start_server(handle_client, '0.0.0.0', PORT_TCP)
    log.info("==================================================")
    log.info(f"🚀 Servidor ORAS MMO ACTIVO en el puerto {PORT_TCP} (TCP)")
    log.info("   Crossplay: Azahar PC y Azahar Android")
    log.info("   Esperando conexiones de jugadores...")
    log.info("==================================================")
    
    async with tcp_server:
        await tcp_server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("Servidor detenido por el usuario.")
