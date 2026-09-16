"""
server.py — Project ORAS MMO WebSocket Relay Server v3
========================================================
v3 changes vs v2 (QA-certified):
  OPT-1  Zone-based relay: position packets are ONLY relayed to players
         on the same map_id. Cross-zone traffic is silently dropped.
         This eliminates all unnecessary bandwidth for large maps.
  OPT-2  Per-player position packet rate limiter: max MAX_POS_RATE/sec
         (default 20/sec). Bursts beyond this are dropped server-side.
         Prevents one fast-moving player from flooding 5 receivers.
  OPT-3  Packet format extended: 8 bytes of reserved_future repurposed
         as vel_x, vel_y (float32 each) for client-side dead reckoning.
         Remaining 26 bytes stay zero-padded for future use.
         Dead reckoning lets remote NPCs interpolate smoothly between
         position packets — no jitter even at 100ms+ latency.
  OPT-4  Stationary culling flag: if flags bit4=1, packet is a
         "heartbeat" (player hasn't moved). Server relays it at 1/sec
         max instead of full rate. Client sets this when delta < 0.5u.

All v2 QA fixes remain intact (FAIL-1 through WARN-11).

Protocol: Fixed 64-byte little-endian binary packets.

Packet layout (v3):
  Offset  Size  Field
  0       1     packet_type
  1       1     player_id
  2       1     locale          (0=EN, 1=ES)
  3       1     reserved
  4       4     map_id          (uint32)
  8       4     pos_x           (float32)
  12      4     pos_y           (float32)
  16      4     pos_z           (float32)
  20      1     facing
  21      1     anim_frame
  22      1     battle_type
  23      1     flags           (bit0=in_cutscene, bit1=in_battle,
                                 bit2=in_menu, bit3=reserved,
                                 bit4=is_stationary ← NEW OPT-4)
  24      6     pokemon_levels
  30      4     vel_x           (float32) ← NEW OPT-3
  34      4     vel_y           (float32) ← NEW OPT-3
  38      26    reserved_future (zero-padded)
Total: 64 bytes.

Environment variables:
    PORT                TCP port to listen on               (default: 8765)
    MAX_ROOMS           Max concurrent room count            (default: 100)
    MAX_CONNS_PER_IP    Max concurrent WS connections/IP     (default: 3)
    MAX_POS_RATE        Max position packets per player/sec  (default: 20)
    STATIONARY_RATE     Max heartbeat packets per player/sec (default: 1)
    LOG_LEVEL           Logging verbosity DEBUG/INFO         (default: INFO)

Author: Project ORAS MMO — Backend Agent + Lead Architect (v3 movement opt)
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
PORT:              int = int(os.environ.get("PORT", 8765))
MAX_ROOMS:         int = int(os.environ.get("MAX_ROOMS", 100))
MAX_CONNS_PER_IP:  int = int(os.environ.get("MAX_CONNS_PER_IP", 3))
MAX_POS_RATE:      int = int(os.environ.get("MAX_POS_RATE", 20))   # OPT-2
STATIONARY_RATE:   int = int(os.environ.get("STATIONARY_RATE", 1)) # OPT-4
LOG_LEVEL:         str = os.environ.get("LOG_LEVEL", "INFO").upper()

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
PKT_BATTLE_REQUEST = 0x20
PKT_TRADE_REQUEST  = 0x21
PKT_PING           = 0xFF
PKT_PONG           = 0xFE  # distinct opcode — FAIL-1 fix retained

LOCALE_EN = 0
LOCALE_ES = 1

# OPT-4: flag bit that marks a packet as a stationary heartbeat
FLAG_STATIONARY = 0x10  # bit4 of flags byte

# v3 packet: vel_x + vel_y added in first 8 bytes of reserved_future
# Layout: BBBBIfffBBBB6s | ff | 26s
#         (30 bytes)     |vel |  padding
# Total:  30 + 4 + 4 + 26 = 64 ✓
PACKET_STRUCT = struct.Struct("<BBBBIfffBBBB6sff26s")
assert PACKET_STRUCT.size == PACKET_SIZE, (
    f"PACKET_STRUCT size mismatch: {PACKET_STRUCT.size} != {PACKET_SIZE}"
)

# ---------------------------------------------------------------------------
# State classes
# ---------------------------------------------------------------------------

class PlayerState:
    """Per-connection mutable state, including zone and rate-limit tracking."""

    def __init__(self, ws: WebSocketServerProtocol, player_id: int, room_code: str) -> None:
        self.ws          = ws
        self.player_id   = player_id
        self.room_code   = room_code
        self.connected_at = time.monotonic()
        self.last_ping   = time.monotonic()
        self.in_battle   = False
        self.in_cutscene = False
        self.locale      = LOCALE_EN

        # OPT-1: zone tracking — only relay position to same-map players
        self.map_id: int = 0

        # OPT-2: position packet rate limiter (token bucket, 1-token = 1 packet)
        self._pos_tokens:        float = float(MAX_POS_RATE)
        self._pos_last_refill:   float = time.monotonic()

        # OPT-4: stationary heartbeat rate limiter (1/sec)
        self._stat_tokens:       float = float(STATIONARY_RATE)
        self._stat_last_refill:  float = time.monotonic()

    # --- Token-bucket rate limiter helpers ---

    def _refill_tokens(self) -> None:
        """Refill both token buckets based on elapsed time."""
        now = time.monotonic()

        elapsed_pos = now - self._pos_last_refill
        self._pos_tokens = min(
            float(MAX_POS_RATE),
            self._pos_tokens + elapsed_pos * MAX_POS_RATE,
        )
        self._pos_last_refill = now

        elapsed_stat = now - self._stat_last_refill
        self._stat_tokens = min(
            float(STATIONARY_RATE),
            self._stat_tokens + elapsed_stat * STATIONARY_RATE,
        )
        self._stat_last_refill = now

    def consume_position_token(self, is_stationary: bool) -> bool:
        """
        OPT-2 / OPT-4: Try to consume a rate-limit token.
        Returns True if the packet should be forwarded, False if it should be dropped.
        Stationary heartbeats use a separate, much slower bucket (1/sec).
        """
        self._refill_tokens()
        if is_stationary:
            if self._stat_tokens >= 1.0:
                self._stat_tokens -= 1.0
                return True
            return False  # drop: stationary heartbeat exceeds 1/sec
        else:
            if self._pos_tokens >= 1.0:
                self._pos_tokens -= 1.0
                return True
            return False  # drop: moving too fast for configured rate


class Room:
    """Holds up to MAX_PLAYERS concurrent PlayerState objects."""

    MAX_PLAYERS = 6

    def __init__(self, code: str) -> None:
        self.code    = code
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

    def same_zone_players(self, exclude_id: int, map_id: int):
        """
        OPT-1: Return only players on the same map_id as the sender.
        Players on different maps never receive position updates.
        """
        return [
            p for pid, p in self.players.items()
            if pid != exclude_id and p.map_id == map_id
        ]


# ---------------------------------------------------------------------------
# Global server state
# ---------------------------------------------------------------------------
rooms: Dict[str, Room] = {}
_ip_conn_count: Dict[str, int] = defaultdict(int)
_diag_task: Optional[asyncio.Task] = None

# ---------------------------------------------------------------------------
# Packet builders / parsers
# ---------------------------------------------------------------------------

def build_packet(
    pkt_type:       int,
    player_id:      int,
    locale:         int   = LOCALE_EN,
    map_id:         int   = 0,
    pos_x:          float = 0.0,
    pos_y:          float = 0.0,
    pos_z:          float = 0.0,
    facing:         int   = 0,
    anim_frame:     int   = 0,
    battle_type:    int   = 0,
    flags:          int   = 0,
    pokemon_levels: bytes = b"\x00" * 6,
    vel_x:          float = 0.0,   # OPT-3: dead reckoning X velocity
    vel_y:          float = 0.0,   # OPT-3: dead reckoning Y velocity
) -> bytes:
    pokemon_levels = (pokemon_levels + b"\x00" * 6)[:6]  # safe pad/truncate
    return PACKET_STRUCT.pack(
        pkt_type, player_id, locale, 0,
        map_id,
        pos_x, pos_y, pos_z,
        facing, anim_frame, battle_type, flags,
        pokemon_levels,
        vel_x, vel_y,       # OPT-3
        b"\x00" * 26,       # reserved_future
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
    return build_packet(PKT_PONG, player_id)

# ---------------------------------------------------------------------------
# Broadcast helpers
# ---------------------------------------------------------------------------

async def _safe_send(player: PlayerState, data: bytes) -> None:
    try:
        await player.ws.send(data)
    except Exception:
        pass


async def broadcast(room: Room, data: bytes, exclude_id: int) -> None:
    """Broadcast to ALL other players in room (used for non-position events)."""
    targets = room.other_players(exclude_id)
    if not targets:
        return
    await asyncio.gather(*[_safe_send(p, data) for p in targets], return_exceptions=True)


async def broadcast_zone(room: Room, data: bytes, exclude_id: int, map_id: int) -> None:
    """
    OPT-1: Broadcast ONLY to players on the same map_id.
    Used for position packets — players on other maps are skipped entirely.
    """
    targets = room.same_zone_players(exclude_id, map_id)
    if not targets:
        return
    await asyncio.gather(*[_safe_send(p, data) for p in targets], return_exceptions=True)

# ---------------------------------------------------------------------------
# Lock-state helpers
# ---------------------------------------------------------------------------

async def _broadcast_unlock_if_locked(room: Room, state: PlayerState) -> None:
    """Release ghost lock: send PKT_UNLOCK if player was in battle/cutscene."""
    if state.in_battle or state.in_cutscene:
        unlock_pkt = make_unlock_packet(state.player_id, locale=state.locale)
        await broadcast(room, unlock_pkt, exclude_id=state.player_id)
        state.in_battle   = False
        state.in_cutscene = False

# ---------------------------------------------------------------------------
# Packet handlers
# ---------------------------------------------------------------------------

async def handle_position(
    room: Room, state: PlayerState, raw: bytes,
    map_id: int, flags: int,
) -> None:
    """
    OPT-1 + OPT-2 + OPT-4:
    - Update player's known map_id for zone filtering.
    - Apply rate limiter before relaying.
    - Use zone-only broadcast.
    """
    state.map_id = map_id  # OPT-1: keep zone current

    is_stationary = bool(flags & FLAG_STATIONARY)  # OPT-4

    # OPT-2: drop packet if player is sending faster than allowed rate
    if not state.consume_position_token(is_stationary):
        return  # silently drop — client keeps moving locally regardless

    # OPT-1: only relay to players on the same map
    await broadcast_zone(room, raw, exclude_id=state.player_id, map_id=map_id)


async def handle_battle_start(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_battle = True
    log.info("Player %d in room %s entered battle (map %d)", state.player_id, room.code, state.map_id)
    await broadcast(room, raw, exclude_id=state.player_id)
    await broadcast(room, make_lock_packet(state.player_id, locale=state.locale), exclude_id=state.player_id)


async def handle_battle_end(room: Room, state: PlayerState, raw: bytes) -> None:
    state.in_battle = False
    log.info("Player %d in room %s exited battle", state.player_id, room.code)
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


async def handle_map_change(room: Room, state: PlayerState, raw: bytes, map_id: int) -> None:
    """
    OPT-1 extended: update zone immediately on map change.
    Clear any active lock (player can't be in battle mid-teleport).
    Broadcast to ALL players (not zone-filtered) so everyone knows the
    player changed map — they need to hide/show the NPC accordingly.
    """
    await _broadcast_unlock_if_locked(room, state)
    state.map_id = map_id  # OPT-1: update zone
    log.info("Player %d moved to map %d", state.player_id, map_id)
    # Map-change is broadcast to everyone (not zone-filtered) so remote
    # clients know to remove/add the NPC from their overworld.
    await broadcast(room, raw, exclude_id=state.player_id)

# ---------------------------------------------------------------------------
# Handshake
# ---------------------------------------------------------------------------

async def perform_handshake(
    ws: WebSocketServerProtocol,
) -> Optional[Tuple[str, int]]:
    """
    Text-frame handshake: client sends "ROOMCODE:0" (player_hint ignored).
    Returns (room_code, player_id) or None.
    Raw input is never reflected in error messages (WARN-7).
    Timeout: 5 s (WARN-8).
    """
    try:
        raw = await asyncio.wait_for(ws.recv(), timeout=5.0)
    except (asyncio.TimeoutError, Exception):
        return None

    if not isinstance(raw, str):
        return None

    parts     = raw.strip().split(":")
    room_code = parts[0].upper() if parts else ""

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

    await ws.send(f"OK:{room_code}:{player_id}")
    return room_code, player_id

# ---------------------------------------------------------------------------
# Main connection handler
# ---------------------------------------------------------------------------

async def handle_connection(ws: WebSocketServerProtocol) -> None:
    remote_ip = ws.remote_address[0]
    remote    = f"{remote_ip}:{ws.remote_address[1]}"

    # Per-IP connection cap (WARN-8)
    _ip_conn_count[remote_ip] += 1
    if _ip_conn_count[remote_ip] > MAX_CONNS_PER_IP:
        log.warning("IP %s exceeded MAX_CONNS_PER_IP=%d — rejecting", remote_ip, MAX_CONNS_PER_IP)
        try:
            await ws.close(1008, "Too many connections from your IP")
        except Exception:
            pass
        _ip_conn_count[remote_ip] -= 1
        return

    state:      Optional[PlayerState] = None
    room_code:  Optional[str]         = None

    try:
        result = await perform_handshake(ws)
        if result is None:
            return

        room_code, player_id = result

        # Re-validate room after handshake await (FAIL-3 fix)
        room = rooms.get(room_code)
        if room is None:
            log.warning("Room %s expired during join for %s", room_code, remote)
            await ws.send("ERROR:ROOM_EXPIRED")
            return

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

            (
                pkt_type, pkt_player_id, locale, _reserved,
                map_id, pos_x, pos_y, pos_z,
                facing, anim_frame, battle_type, flags,
                pokemon_levels,
                vel_x, vel_y,      # OPT-3
                _reserved_future,
            ) = parsed

            # Player-ID spoof guard (FAIL-2 fix)
            if pkt_player_id != player_id:
                log.warning(
                    "Spoof attempt: player %d sent pkt_player_id=%d — dropped",
                    player_id, pkt_player_id,
                )
                continue

            state.locale    = locale
            state.last_ping = time.monotonic()

            # --- Packet dispatch ---
            if pkt_type == PKT_PING:
                await _safe_send(state, make_pong_packet(player_id))

            elif pkt_type == PKT_POSITION:
                # OPT-1 + OPT-2 + OPT-4: zone-filtered, rate-limited relay
                await handle_position(room, state, message, map_id, flags)

            elif pkt_type == PKT_BATTLE_START:
                await handle_battle_start(room, state, message)

            elif pkt_type == PKT_BATTLE_END:
                await handle_battle_end(room, state, message)

            elif pkt_type == PKT_CUTSCENE_START:
                await handle_cutscene_start(room, state, message)

            elif pkt_type == PKT_CUTSCENE_END:
                await handle_cutscene_end(room, state, message)

            elif pkt_type == PKT_MAP_CHANGE:
                await handle_map_change(room, state, message, map_id)

            elif pkt_type in (PKT_BATTLE_REQUEST, PKT_TRADE_REQUEST):
                target_id = anim_frame
                target_state = room.players.get(target_id)
                if target_state:
                    await _safe_send(target_state, message)

            else:
                # Unknown future packet types: plain relay to all
                await broadcast(room, message, exclude_id=player_id)

    except websockets.exceptions.ConnectionClosedOK:
        pass
    except websockets.exceptions.ConnectionClosedError:
        pass
    except Exception as exc:
        pid_str = str(state.player_id) if state else "?"
        log.error("Unexpected error for player %s: %s", pid_str, exc, exc_info=True)
    finally:
        _ip_conn_count[remote_ip] = max(0, _ip_conn_count[remote_ip] - 1)

        if state is not None and room_code is not None:
            room = rooms.get(room_code)
            if room is not None:
                room.remove_player(state.player_id)

                if room.players:
                    await _broadcast_unlock_if_locked(room, state)  # FAIL-4 fix
                    disc_pkt = make_disconnect_packet(state.player_id)
                    await broadcast(room, disc_pkt, exclude_id=state.player_id)

                if not room.players and room_code in rooms:
                    del rooms[room_code]
                    log.info("Room %s destroyed (empty)", room_code)

        try:
            await ws.close()
        except Exception:
            pass

# ---------------------------------------------------------------------------
# Background diagnostics
# ---------------------------------------------------------------------------

async def diagnostics_loop(interval: int = 30) -> None:
    """Periodic server health summary. Exception-guarded (FAIL-5/6 fixes)."""
    while True:
        try:
            await asyncio.sleep(interval)
            snapshot = list(rooms.items())  # atomic snapshot (FAIL-6)
            if snapshot:
                parts = []
                for code, r in snapshot:
                    zones = set(p.map_id for p in r.players.values())
                    parts.append(f"{code}({len(r.players)}p,{len(zones)}zones)")
                log.info("Active rooms: %s", ", ".join(parts))
            else:
                log.info("No active rooms.")
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            log.error("diagnostics_loop error (continuing): %s", exc)

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

async def main() -> None:
    global _diag_task

    log.info("=" * 62)
    log.info("  Project ORAS MMO — WebSocket Relay Server v3")
    log.info("  Port: %d | MAX_ROOMS: %d | MAX_CONNS_PER_IP: %d", PORT, MAX_ROOMS, MAX_CONNS_PER_IP)
    log.info("  MAX_POS_RATE: %d/s | STATIONARY_RATE: %d/s", MAX_POS_RATE, STATIONARY_RATE)
    log.info("  Features: zone-filter ON | rate-limit ON | dead-reckoning ON")
    log.info("=" * 62)

    _diag_task = asyncio.create_task(diagnostics_loop(interval=30))

    try:
        async with websockets.serve(
            handle_connection,
            "0.0.0.0",
            PORT,
            ping_interval=20,
            ping_timeout=60,
            max_size=PACKET_SIZE * 10,
            compression=None,           # raw binary — no deflate overhead
        ):
            log.info("Server ready. Waiting for connections...")
            await asyncio.Future()      # run until Ctrl+C
    finally:
        if _diag_task and not _diag_task.done():
            _diag_task.cancel()
            try:
                await _diag_task
            except asyncio.CancelledError:
                pass
        log.info("Server shut down cleanly.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
