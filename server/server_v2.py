"""
server_v2.py — Project ORAS MMO WebSocket Relay Server (QA-Patched)
====================================================================
Fixes applied vs server.py (QA Report 2026-09-14):

  FAIL-1  PKT_PONG given distinct opcode 0xFE (was 0xFF = PKT_PING clash).
  FAIL-2  pkt_player_id validated against server-assigned player_id every packet.
  FAIL-3  Room re-validated after handshake yields; ws.close() on stale-room path.
  FAIL-4  Ghost-lock cleared: unlock broadcast sent on disconnect if in_battle/in_cutscene.
  FAIL-5  diagnostics_loop task reference stored; inner loop guarded with try/except.
  FAIL-6  rooms dict snapshotted (list(rooms.items())) before iteration in diagnostics.
  WARN-7  Raw room_code no longer reflected in error responses.
  WARN-8  Per-IP connection cap (MAX_CONNS_PER_IP=3); handshake timeout reduced to 5 s.
           NOTE: full token-bucket rate limiting is deferred to a future middleware layer.
  WARN-9  PKT_MAP_CHANGE clears in_battle/in_cutscene state and sends PKT_UNLOCK if needed.
  WARN-10 pokemon_levels safely padded/truncated to exactly 6 bytes in build_packet.
  WARN-11 handle_connection wrapped in try/finally that always closes ws.

Protocol: Fixed 64-byte little-endian binary packets (no JSON, no text frames).

Environment variables:
    PORT                TCP port to listen on               (default: 8765)
    MAX_ROOMS           Max concurrent room count            (default: 100)
    MAX_CONNS_PER_IP    Max concurrent WS connections/IP     (default: 3)
    LOG_LEVEL           Logging verbosity DEBUG/INFO         (default: INFO)

Author: Project ORAS MMO Backend Agent (patched by QA & Review Agent)
"""

import asyncio
import logging
import os
import struct
import time
from collections import defaultdict
from typing import Dict, Optional, Tuple

import websockets
from websockets.server import WebSocketServerProtocol

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
PORT: int = int(os.environ.get("PORT", 8765))
MAX_ROOMS: int = int(os.environ.get("MAX_ROOMS", 100))
MAX_CONNS_PER_IP: int = int(os.environ.get("MAX_CONNS_PER_IP", 3))
LOG_LEVEL: str = os.environ.get("LOG_LEVEL", "INFO").upper()

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("oras_server")

# ---------------------------------------------------------------------------
# Packet protocol constants
# ---------------------------------------------------------------------------
PACKET_SIZE = 64

PKT_POSITION       = 0x01
PKT_BATTLE_START   = 0x02
PKT_BATTLE_END     = 0x03
PKT_CUTSCENE_START = 0x04
PKT_CUTSCENE_END   = 0x05
PKT_MAP_CHANGE     = 0x06
PKT_DISCONNECT     = 0x10
PKT_LOCK           = 0x11
PKT_UNLOCK         = 0x12
PKT_PING           = 0xFF
PKT_PONG           = 0xFE  # FIX-FAIL-1: distinct from PKT_PING (was 0xFF — caused infinite loop)

LOCALE_EN = 0
LOCALE_ES = 1

PACKET_STRUCT = struct.Struct("<BBBBIfffBBBB6s34s")
assert PACKET_STRUCT.size == PACKET_SIZE, (
    f"PACKET_STRUCT size mismatch: {PACKET_STRUCT.size} != {PACKET_SIZE}"
)

# ---------------------------------------------------------------------------
# State classes
# ---------------------------------------------------------------------------

class PlayerState:
    """Per-connection mutable state."""

    def __init__(self, ws: WebSocketServerProtocol, player_id: int, room_code: str) -> None:
        self.ws = ws
        self.player_id = player_id
        self.room_code = room_code
        self.connected_at = time.monotonic()
        self.last_ping = time.monotonic()
        self.in_battle = False
        self.in_cutscene = False
        self.locale = LOCALE_EN


class Room:
    """Holds up to MAX_PLAYERS concurrent PlayerState objects."""

    MAX_PLAYERS = 6

    def __init__(self, code: str) -> None:
        self.code = code
        self.players: Dict[int, PlayerState] = {}
        self.created_at = time.monotonic()

    def is_full(self) -> bool:
        return len(self.players) >= self.MAX_PLAYERS

    def next_player_id(self) -> Optional[int]:
        used = set(self.players.keys())
        for pid in range(1, self.MAX_PLAYERS + 1):
            if pid not in used:
                return pid
        return None

    def add_player(self, state: PlayerState) -> None:
        self.players[state.player_id] = state

    def remove_player(self, player_id: int) -> Optional[PlayerState]:
        return self.players.pop(player_id, None)

    def other_players(self, exclude_id: int):
        return [p for pid, p in self.players.items() if pid != exclude_id]


# ---------------------------------------------------------------------------
# Global server state
# ---------------------------------------------------------------------------
rooms: Dict[str, Room] = {}

# FIX-WARN-8: per-IP connection counter to cap connection floods
_ip_conn_count: Dict[str, int] = defaultdict(int)

# FIX-FAIL-5: hold strong reference to diagnostics task
_diag_task: Optional[asyncio.Task] = None

# ---------------------------------------------------------------------------
# Packet builders / parsers
# ---------------------------------------------------------------------------

def build_packet(
    pkt_type: int,
    player_id: int,
    locale: int = LOCALE_EN,
    map_id: int = 0,
    pos_x: float = 0.0,
    pos_y: float = 0.0,
    pos_z: float = 0.0,
    facing: int = 0,
    anim_frame: int = 0,
    battle_type: int = 0,
    flags: int = 0,
    pokemon_levels: bytes = b"\x00" * 6,
) -> bytes:
    # FIX-WARN-10: safely pad/truncate pokemon_levels to exactly 6 bytes
    pokemon_levels = (pokemon_levels + b"\x00" * 6)[:6]
    return PACKET_STRUCT.pack(
        pkt_type, player_id, locale, 0, map_id,
        pos_x, pos_y, pos_z, facing, anim_frame, battle_type, flags,
        pokemon_levels, b"\x00" * 34,
    )


def parse_packet(data: bytes) -> Optional[tuple]:
    if len(data) != PACKET_SIZE:
        return None
    try:
        return PACKET_STRUCT.unpack(data)
    except struct.error:
        return None


def make_disconnect_packet(player_id: int) -> bytes:
    return build_packet(PKT_DISCONNECT, player_id)

def make_lock_packet(player_id: int, locale: int = LOCALE_EN) -> bytes:
    return build_packet(PKT_LOCK, player_id, locale=locale)

def make_unlock_packet(player_id: int, locale: int = LOCALE_EN) -> bytes:
    return build_packet(PKT_UNLOCK, player_id, locale=locale)

def make_pong_packet(player_id: int) -> bytes:
    # FIX-FAIL-1: uses PKT_PONG = 0xFE, not 0xFF
    return build_packet(PKT_PONG, player_id)

# ---------------------------------------------------------------------------
# Broadcast helpers
# ---------------------------------------------------------------------------

async def _safe_send(player: PlayerState, data: bytes) -> None:
    """Send data to one player, silently absorbing any WebSocket errors."""
    try:
        await player.ws.send(data)
    except Exception:
        pass


async def broadcast(room: Room, data: bytes, exclude_id: int) -> None:
    """Broadcast data to all players in room except exclude_id."""
    targets = room.other_players(exclude_id)
    if not targets:
        return
    await asyncio.gather(*[_safe_send(p, data) for p in targets], return_exceptions=True)

# ---------------------------------------------------------------------------
# Lock-state helpers
# ---------------------------------------------------------------------------

async def _broadcast_unlock_if_locked(room: Room, state: PlayerState) -> None:
    """If state holds an active lock, broadcast PKT_UNLOCK to release other players."""
    if state.in_battle or state.in_cutscene:
        unlock_pkt = make_unlock_packet(state.player_id, locale=state.locale)
        await broadcast(room, unlock_pkt, exclude_id=state.player_id)
        state.in_battle = False
        state.in_cutscene = False

# ---------------------------------------------------------------------------
# Packet handlers
# ---------------------------------------------------------------------------

async def handle_battle_start(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_battle = True
    await broadcast(room, raw, exclude_id=state.player_id)
    await broadcast(room, make_lock_packet(state.player_id, locale=state.locale), exclude_id=state.player_id)


async def handle_battle_end(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_battle = False
    await broadcast(room, raw, exclude_id=state.player_id)
    await broadcast(room, make_unlock_packet(state.player_id, locale=state.locale), exclude_id=state.player_id)


async def handle_cutscene_start(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_cutscene = True
    await broadcast(room, raw, exclude_id=state.player_id)
    await broadcast(room, make_lock_packet(state.player_id, locale=state.locale), exclude_id=state.player_id)


async def handle_cutscene_end(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_cutscene = False
    await broadcast(room, raw, exclude_id=state.player_id)
    await broadcast(room, make_unlock_packet(state.player_id, locale=state.locale), exclude_id=state.player_id)


async def handle_map_change(room: Room, state: PlayerState, raw: bytes) -> None:
    """
    FIX-WARN-9: PKT_MAP_CHANGE now clears any active battle/cutscene lock before
    forwarding, preventing ghost-lock when a player changes map mid-battle.
    TODO: add map_id allowlist validation here once map IDs are finalised.
    """
    await _broadcast_unlock_if_locked(room, state)
    await broadcast(room, raw, exclude_id=state.player_id)

# ---------------------------------------------------------------------------
# Handshake
# ---------------------------------------------------------------------------

async def perform_handshake(ws: WebSocketServerProtocol) -> Optional[Tuple[str, int]]:
    """
    Perform text-frame handshake: client sends "ROOM:locale?" (locale optional).
    Returns (room_code, player_id) on success, None on failure.

    FIX-WARN-7: raw input is NOT reflected in error messages.
    FIX-WARN-8: handshake timeout reduced from 10 s to 5 s.
    """
    try:
        raw = await asyncio.wait_for(ws.recv(), timeout=5.0)  # was 10.0
    except (asyncio.TimeoutError, Exception):
        return None

    if not isinstance(raw, str):
        return None

    parts = raw.strip().split(":")
    if not parts:
        await ws.send("ERROR:INVALID_HANDSHAKE")
        return None

    room_code = parts[0].upper()

    # FIX-WARN-7: validate first, never reflect raw input in error
    if not (len(room_code) == 4 and room_code.isalnum()):
        await ws.send("ERROR:INVALID_ROOM_CODE")
        return None

    if room_code not in rooms and len(rooms) >= MAX_ROOMS:
        await ws.send("ERROR:SERVER_FULL")
        return None

    if room_code not in rooms:
        rooms[room_code] = Room(room_code)

    room = rooms[room_code]

    if room.is_full():
        await ws.send("ERROR:ROOM_FULL")
        return None

    player_id = room.next_player_id()
    if player_id is None:
        await ws.send("ERROR:ROOM_FULL")
        return None

    # This await yields to event loop — room may be deleted before we resume.
    # (room re-validation happens in handle_connection after we return.)
    await ws.send(f"OK:{room_code}:{player_id}")
    return room_code, player_id

# ---------------------------------------------------------------------------
# Main connection handler
# ---------------------------------------------------------------------------

async def handle_connection(ws: WebSocketServerProtocol) -> None:
    """
    FIX-WARN-11: entire function body wrapped in try/finally that always closes ws.
    FIX-FAIL-3: room re-validated after handshake to handle the race where the room
                 is deleted while ws.send("OK:...") is awaited in perform_handshake.
    FIX-WARN-8: per-IP connection cap enforced at entry.
    """
    remote_ip = ws.remote_address[0]
    remote = f"{remote_ip}:{ws.remote_address[1]}"

    # FIX-WARN-8: enforce per-IP connection cap
    _ip_conn_count[remote_ip] += 1
    if _ip_conn_count[remote_ip] > MAX_CONNS_PER_IP:
        log.warning("IP %s exceeded MAX_CONNS_PER_IP=%d — rejecting", remote_ip, MAX_CONNS_PER_IP)
        try:
            await ws.close(1008, "Too many connections from your IP")
        except Exception:
            pass
        _ip_conn_count[remote_ip] -= 1
        return

    state: Optional[PlayerState] = None
    room_code: Optional[str] = None

    try:
        result = await perform_handshake(ws)
        if result is None:
            return   # ws.close() in finally

        room_code, player_id = result

        # FIX-FAIL-3: re-validate room after handshake await yields
        room = rooms.get(room_code)
        if room is None:
            log.warning("Room %s expired during join for %s", room_code, remote)
            await ws.send("ERROR:ROOM_EXPIRED")
            return   # ws.close() in finally

        state = PlayerState(ws, player_id, room_code)
        room.add_player(state)
        log.info("Player %d joined room %s from %s", player_id, room_code, remote)

        async for message in ws:
            if not isinstance(message, bytes):
                continue
            if len(message) != PACKET_SIZE:
                continue

            parsed = parse_packet(message)
            if parsed is None:
                continue

            (pkt_type, pkt_player_id, locale, _reserved,
             map_id, pos_x, pos_y, pos_z,
             facing, anim_frame, battle_type, flags,
             pokemon_levels, _reserved_future) = parsed

            # FIX-FAIL-2: reject any packet whose pkt_player_id != server-assigned player_id
            if pkt_player_id != player_id:
                log.warning(
                    "Spoof attempt: player %d sent pkt_player_id=%d — packet dropped",
                    player_id, pkt_player_id,
                )
                continue

            state.locale = locale
            state.last_ping = time.monotonic()

            if pkt_type == PKT_PING:
                await _safe_send(state, make_pong_packet(player_id))
            elif pkt_type == PKT_BATTLE_START:
                await handle_battle_start(room, state, message)
            elif pkt_type == PKT_BATTLE_END:
                await handle_battle_end(room, state, message)
            elif pkt_type == PKT_CUTSCENE_START:
                await handle_cutscene_start(room, state, message)
            elif pkt_type == PKT_CUTSCENE_END:
                await handle_cutscene_end(room, state, message)
            elif pkt_type == PKT_MAP_CHANGE:
                # FIX-WARN-9: handled separately to clear lock state
                await handle_map_change(room, state, message)
            else:
                await broadcast(room, message, exclude_id=player_id)

    except websockets.exceptions.ConnectionClosedOK:
        pass
    except websockets.exceptions.ConnectionClosedError:
        pass
    except Exception as exc:
        pid_str = str(state.player_id) if state else "?"
        log.error("Unexpected error for player %s: %s", pid_str, exc, exc_info=True)
    finally:
        # FIX-WARN-8: decrement per-IP counter
        _ip_conn_count[remote_ip] = max(0, _ip_conn_count[remote_ip] - 1)

        if state is not None and room_code is not None:
            room = rooms.get(room_code)
            if room is not None:
                room.remove_player(state.player_id)

                if room.players:
                    # FIX-FAIL-4: release ghost lock before sending disconnect notification
                    await _broadcast_unlock_if_locked(room, state)
                    disc_pkt = make_disconnect_packet(state.player_id)
                    await broadcast(room, disc_pkt, exclude_id=state.player_id)

                if not room.players and room_code in rooms:
                    del rooms[room_code]
                    log.info("Room %s destroyed (empty)", room_code)

        # FIX-WARN-11: always close the WebSocket, even on exception paths
        try:
            await ws.close()
        except Exception:
            pass

# ---------------------------------------------------------------------------
# Background diagnostics
# ---------------------------------------------------------------------------

async def diagnostics_loop(interval: int = 30) -> None:
    """
    FIX-FAIL-5: exception-guarded inner loop (won't die on transient error).
    FIX-FAIL-6: takes a snapshot of rooms.items() to avoid RuntimeError on mutation.
    """
    while True:
        try:
            await asyncio.sleep(interval)
            snapshot = list(rooms.items())   # FIX-FAIL-6: atomic snapshot
            if snapshot:
                summary = ", ".join(f"{c}({len(r.players)}p)" for c, r in snapshot)
                log.info("Active rooms: %s", summary)
            else:
                log.info("No active rooms.")
        except asyncio.CancelledError:
            raise   # propagate cancellation cleanly
        except Exception as exc:
            log.error("diagnostics_loop error (continuing): %s", exc)

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

async def main() -> None:
    global _diag_task

    log.info("Project ORAS MMO WebSocket Relay Server v2 starting on port %d", PORT)
    log.info("MAX_ROOMS=%d  MAX_CONNS_PER_IP=%d", MAX_ROOMS, MAX_CONNS_PER_IP)

    # FIX-FAIL-5: store task reference to prevent silent GC
    _diag_task = asyncio.create_task(diagnostics_loop(interval=30))

    try:
        async with websockets.serve(
            handle_connection,
            "0.0.0.0",
            PORT,
            ping_interval=20,
            ping_timeout=60,
            max_size=PACKET_SIZE * 10,
            compression=None,
        ):
            await asyncio.Future()   # run until cancelled
    finally:
        if _diag_task and not _diag_task.done():
            _diag_task.cancel()
            try:
                await _diag_task
            except asyncio.CancelledError:
                pass
        log.info("Server shut down.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
