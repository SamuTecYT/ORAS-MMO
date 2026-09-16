"""
server.py — Project ORAS MMO WebSocket Relay Server
=====================================================
Handles up to 6 players per room with a fixed 64-byte binary packet protocol.
Supports battle/cutscene locking, graceful disconnects, and heartbeat pings.

Protocol: Fixed 64-byte little-endian binary packets (no JSON, no text frames).

Environment variables:
    PORT        TCP port to listen on          (default: 8765)
    MAX_ROOMS   Max concurrent room count       (default: 100)
    LOG_LEVEL   Logging verbosity DEBUG/INFO    (default: INFO)

Author: Project ORAS MMO Backend Agent
"""

import asyncio
import logging
import os
import struct
import time
from typing import Dict, Optional

import websockets
from websockets.server import WebSocketServerProtocol

# ---------------------------------------------------------------------------
# Configuration (override via environment variables)
# ---------------------------------------------------------------------------
PORT: int = int(os.environ.get("PORT", 8765))
MAX_ROOMS: int = int(os.environ.get("MAX_ROOMS", 100))
LOG_LEVEL: str = os.environ.get("LOG_LEVEL", "INFO").upper()

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("oras_server")

# ---------------------------------------------------------------------------
# Packet constants
# ---------------------------------------------------------------------------
PACKET_SIZE = 64  # ALL packets are exactly 64 bytes — never deviate

# Packet type byte (offset 0)
PKT_POSITION       = 0x01
PKT_BATTLE_START   = 0x02
PKT_BATTLE_END     = 0x03
PKT_CUTSCENE_START = 0x04
PKT_CUTSCENE_END   = 0x05
PKT_MAP_CHANGE     = 0x06
PKT_PING           = 0xFF

# Server-generated synthetic packet types (sent to clients, never received)
PKT_PONG           = 0xFF  # Pong reuses 0xFF — same player_id echo
PKT_DISCONNECT     = 0x10  # Server notifies room that a player left
PKT_LOCK           = 0x11  # Server orders clients to freeze a player's NPC
PKT_UNLOCK         = 0x12  # Server orders clients to unfreeze a player's NPC

# Locale codes (offset 2)
LOCALE_EN = 0
LOCALE_ES = 1

# Packet struct layout:
#   B  = packet_type  (1 byte)
#   B  = player_id    (1 byte)
#   B  = locale       (1 byte)
#   B  = reserved     (1 byte)
#   I  = map_id       (4 bytes, uint32 LE)
#   f  = pos_x        (4 bytes, float32 LE)
#   f  = pos_y        (4 bytes, float32 LE)
#   f  = pos_z        (4 bytes, float32 LE)
#   B  = facing       (1 byte)
#   B  = anim_frame   (1 byte)
#   B  = battle_type  (1 byte)
#   B  = flags        (1 byte)
#   6s = pokemon_levels (6 bytes)
#   34s= reserved_future (34 bytes, zero-padded)
# Total = 1+1+1+1+4+4+4+4+1+1+1+1+6+34 = 64 bytes
PACKET_STRUCT = struct.Struct("<BBBBIfffBBBB6s34s")
assert PACKET_STRUCT.size == PACKET_SIZE, "Packet struct size mismatch!"


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

class PlayerState:
    """Tracks live state for a single connected player."""

    def __init__(self, ws: WebSocketServerProtocol, player_id: int, room_code: str):
        self.ws = ws
        self.player_id = player_id
        self.room_code = room_code
        self.connected_at: float = time.monotonic()
        self.last_ping: float = time.monotonic()
        self.in_battle: bool = False
        self.in_cutscene: bool = False
        self.locale: int = LOCALE_EN  # updated from incoming packets

    @property
    def remote_addr(self) -> str:
        try:
            return f"{self.ws.remote_address[0]}:{self.ws.remote_address[1]}"
        except Exception:
            return "unknown"


class Room:
    """A game session shared by up to 6 players."""

    MAX_PLAYERS = 6

    def __init__(self, code: str):
        self.code = code
        self.players: Dict[int, PlayerState] = {}   # player_id (1-6) -> PlayerState
        self.created_at: float = time.monotonic()

    def is_full(self) -> bool:
        return len(self.players) >= self.MAX_PLAYERS

    def next_player_id(self) -> Optional[int]:
        """Return the lowest available player_id slot (1-6), or None if full."""
        used = set(self.players.keys())
        for pid in range(1, self.MAX_PLAYERS + 1):
            if pid not in used:
                return pid
        return None

    def add_player(self, state: PlayerState) -> None:
        self.players[state.player_id] = state

    def remove_player(self, player_id: int) -> Optional[PlayerState]:
        return self.players.pop(player_id, None)

    def other_players(self, exclude_id: int) -> list:
        """Return all PlayerState objects except the given player_id."""
        return [p for pid, p in self.players.items() if pid != exclude_id]

    def __repr__(self) -> str:
        return f"Room(code={self.code!r}, players={list(self.players.keys())})"


# ---------------------------------------------------------------------------
# Global room registry
# ---------------------------------------------------------------------------
rooms: Dict[str, Room] = {}  # room_code -> Room


# ---------------------------------------------------------------------------
# Packet helpers
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
    """
    Construct a 64-byte binary packet with the given fields.
    reserved and reserved_future are always zero-filled.
    """
    return PACKET_STRUCT.pack(
        pkt_type,
        player_id,
        locale,
        0,                          # reserved byte — always 0
        map_id,
        pos_x,
        pos_y,
        pos_z,
        facing,
        anim_frame,
        battle_type,
        flags,
        pokemon_levels,
        b"\x00" * 34,              # reserved_future — zero-padded
    )


def parse_packet(data: bytes) -> Optional[tuple]:
    """
    Parse a 64-byte binary packet.
    Returns unpacked tuple or None if size is wrong.
    """
    if len(data) != PACKET_SIZE:
        return None
    return PACKET_STRUCT.unpack(data)


def make_disconnect_packet(player_id: int) -> bytes:
    """Build a 0x10 DISCONNECT server packet to notify other players."""
    return build_packet(PKT_DISCONNECT, player_id)


def make_lock_packet(player_id: int, locale: int = LOCALE_EN) -> bytes:
    """Build a 0x11 LOCK server packet — freeze that player's NPC on clients."""
    return build_packet(PKT_LOCK, player_id, locale=locale)


def make_unlock_packet(player_id: int, locale: int = LOCALE_EN) -> bytes:
    """Build a 0x12 UNLOCK server packet — unfreeze that player's NPC on clients."""
    return build_packet(PKT_UNLOCK, player_id, locale=locale)


def make_pong_packet(player_id: int) -> bytes:
    """Build a 0xFF PONG packet echoing back the same player_id."""
    return build_packet(PKT_PONG, player_id)


# ---------------------------------------------------------------------------
# Relay helpers
# ---------------------------------------------------------------------------

async def broadcast(room: Room, data: bytes, exclude_id: int) -> None:
    """
    Send `data` to every player in the room except `exclude_id`.
    Uses asyncio.gather for parallel delivery; ignores send errors on dead sockets.
    """
    targets = room.other_players(exclude_id)
    if not targets:
        return
    coros = [_safe_send(player, data) for player in targets]
    await asyncio.gather(*coros, return_exceptions=True)


async def _safe_send(player: PlayerState, data: bytes) -> None:
    """Send data to a player; silently discard if the connection is closed."""
    try:
        await player.ws.send(data)
    except Exception:
        pass  # Dead sockets are cleaned up by their own handler


# ---------------------------------------------------------------------------
# Battle / cutscene lock logic
# ---------------------------------------------------------------------------

async def handle_battle_start(room: Room, state: PlayerState, raw: bytes) -> None:
    """
    Player entered a battle:
      1. Mark state.in_battle = True
      2. Relay the original packet so others see the animation
      3. Broadcast a LOCK packet so clients freeze that player's NPC
    """
    state.in_battle = True
    log.info("  Player %d in room %r entered battle.", state.player_id, room.code)
    # Relay the original battle_start packet first
    await broadcast(room, raw, exclude_id=state.player_id)
    # Then send the synthetic LOCK packet
    lock_pkt = make_lock_packet(state.player_id, locale=state.locale)
    await broadcast(room, lock_pkt, exclude_id=state.player_id)


async def handle_battle_end(room: Room, state: PlayerState, raw: bytes) -> None:
    """
    Player exited a battle:
      1. Mark state.in_battle = False
      2. Relay original packet
      3. Broadcast UNLOCK so clients restore movement for that NPC
    """
    state.in_battle = False
    log.info("  Player %d in room %r exited battle.", state.player_id, room.code)
    await broadcast(room, raw, exclude_id=state.player_id)
    unlock_pkt = make_unlock_packet(state.player_id, locale=state.locale)
    await broadcast(room, unlock_pkt, exclude_id=state.player_id)


async def handle_cutscene_start(room: Room, state: PlayerState, raw: bytes) -> None:
    """
    Player triggered a cutscene — freeze their NPC for everyone else.
    """
    state.in_cutscene = True
    log.info("  Player %d in room %r started cutscene.", state.player_id, room.code)
    await broadcast(room, raw, exclude_id=state.player_id)
    lock_pkt = make_lock_packet(state.player_id, locale=state.locale)
    await broadcast(room, lock_pkt, exclude_id=state.player_id)


async def handle_cutscene_end(room: Room, state: PlayerState, raw: bytes) -> None:
    """
    Cutscene ended — unfreeze their NPC.
    """
    state.in_cutscene = False
    log.info("  Player %d in room %r ended cutscene.", state.player_id, room.code)
    await broadcast(room, raw, exclude_id=state.player_id)
    unlock_pkt = make_unlock_packet(state.player_id, locale=state.locale)
    await broadcast(room, unlock_pkt, exclude_id=state.player_id)


# ---------------------------------------------------------------------------
# Connection handshake
# ---------------------------------------------------------------------------

async def perform_handshake(ws: WebSocketServerProtocol):
    """
    Expect the very first message to be a plain-text UTF-8 string:
        "<ROOM_CODE>:<PLAYER_HINT>"
    where ROOM_CODE is 4 alphanumeric chars and PLAYER_HINT is ignored
    (server assigns IDs itself to prevent collisions).

    Returns (room_code, assigned_player_id) or None on failure.
    """
    try:
        raw = await asyncio.wait_for(ws.recv(), timeout=10.0)
    except asyncio.TimeoutError:
        log.warning("Handshake timeout from %s", ws.remote_address)
        return None
    except Exception as exc:
        log.warning("Handshake recv error: %s", exc)
        return None

    # The handshake message is text, NOT a binary packet
    if not isinstance(raw, str):
        log.warning("Expected text handshake, got binary from %s", ws.remote_address)
        return None

    parts = raw.strip().split(":")
    room_code = parts[0].upper()

    # Validate room code: exactly 4 alphanumeric characters
    if not (len(room_code) == 4 and room_code.isalnum()):
        await ws.send(f"ERROR:INVALID_ROOM_CODE:{room_code}")
        return None

    # Enforce global room cap
    if room_code not in rooms and len(rooms) >= MAX_ROOMS:
        await ws.send("ERROR:SERVER_FULL")
        return None

    # Get or create room
    if room_code not in rooms:
        rooms[room_code] = Room(room_code)
        log.info("  Created room %r", room_code)

    room = rooms[room_code]

    if room.is_full():
        await ws.send("ERROR:ROOM_FULL")
        return None

    player_id = room.next_player_id()
    if player_id is None:
        await ws.send("ERROR:ROOM_FULL")
        return None

    # Confirm assignment to the connecting client
    await ws.send(f"OK:{room_code}:{player_id}")
    return room_code, player_id


# ---------------------------------------------------------------------------
# Per-connection handler
# ---------------------------------------------------------------------------

async def handle_connection(ws: WebSocketServerProtocol) -> None:
    """
    Lifecycle for one WebSocket connection:
      1. Handshake — assign room + player_id
      2. Receive loop — parse packets, relay or handle special types
      3. Cleanup — remove from room, notify others of disconnect
    """
    remote = f"{ws.remote_address[0]}:{ws.remote_address[1]}"
    log.info("  Incoming connection from %s", remote)

    # --- Handshake ---
    result = await perform_handshake(ws)
    if result is None:
        log.warning("  Handshake failed for %s", remote)
        await ws.close()
        return

    room_code, player_id = result
    room = rooms[room_code]

    state = PlayerState(ws, player_id, room_code)
    room.add_player(state)

    log.info(
        "  Player %d joined room %r (total: %d/6) from %s",
        player_id, room_code, len(room.players), remote,
    )

    try:
        # --- Main receive loop ---
        async for message in ws:
            # We only accept binary frames after handshake
            if not isinstance(message, bytes):
                log.debug("Ignoring non-binary frame from player %d", player_id)
                continue

            if len(message) != PACKET_SIZE:
                log.debug(
                    "Dropped malformed packet (size=%d) from player %d",
                    len(message), player_id,
                )
                continue

            parsed = parse_packet(message)
            if parsed is None:
                continue

            (
                pkt_type, pkt_player_id, locale, _reserved,
                map_id, pos_x, pos_y, pos_z,
                facing, anim_frame, battle_type, flags,
                pokemon_levels, _reserved_future,
            ) = parsed

            # Update locale from every incoming packet
            state.locale = locale
            state.last_ping = time.monotonic()

            # --- Dispatch by packet type ---
            if pkt_type == PKT_PING:
                # Respond with pong directly to sender, do NOT relay
                await _safe_send(state, make_pong_packet(player_id))

            elif pkt_type == PKT_BATTLE_START:
                await handle_battle_start(room, state, message)

            elif pkt_type == PKT_BATTLE_END:
                await handle_battle_end(room, state, message)

            elif pkt_type == PKT_CUTSCENE_START:
                await handle_cutscene_start(room, state, message)

            elif pkt_type == PKT_CUTSCENE_END:
                await handle_cutscene_end(room, state, message)

            else:
                # PKT_POSITION, PKT_MAP_CHANGE, and future types:
                # Simple relay — forward raw bytes to all other players in room
                await broadcast(room, message, exclude_id=player_id)

    except websockets.exceptions.ConnectionClosedOK:
        log.info("  Player %d (room %r) disconnected normally.", player_id, room_code)
    except websockets.exceptions.ConnectionClosedError as exc:
        log.warning("  Player %d (room %r) connection error: %s", player_id, room_code, exc)
    except Exception as exc:
        log.error("  Unexpected error for player %d: %s", player_id, exc, exc_info=True)
    finally:
        # --- Cleanup ---
        room.remove_player(player_id)
        log.info(
            "  Player %d left room %r (remaining: %d/6)",
            player_id, room_code, len(room.players),
        )

        # Notify remaining players about the disconnect
        if room.players:
            disc_pkt = make_disconnect_packet(player_id)
            await broadcast(room, disc_pkt, exclude_id=player_id)

        # Garbage-collect empty rooms to free memory
        if not room.players and room_code in rooms:
            del rooms[room_code]
            log.info("  Room %r destroyed (empty).", room_code)


# ---------------------------------------------------------------------------
# Periodic diagnostics (runs as a background task)
# ---------------------------------------------------------------------------

async def diagnostics_loop(interval: int = 30) -> None:
    """
    Every `interval` seconds, log a summary of active rooms and player counts.
    Helps with server health monitoring without any extra tooling.
    """
    while True:
        await asyncio.sleep(interval)
        if rooms:
            summary = ", ".join(
                f"{code}({len(r.players)}p)" for code, r in rooms.items()
            )
            log.info("  Active rooms: %s", summary)
        else:
            log.info("  No active rooms.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

async def main() -> None:
    log.info("=" * 60)
    log.info("  Project ORAS MMO -- WebSocket Relay Server")
    log.info("  Listening on ws://0.0.0.0:%d", PORT)
    log.info("  Max rooms: %d | Max players/room: 6", MAX_ROOMS)
    log.info("  Packet size: %d bytes (fixed)", PACKET_SIZE)
    log.info("=" * 60)

    # Start background diagnostics task
    asyncio.create_task(diagnostics_loop(interval=30))

    # Start WebSocket server.
    # ping_interval=20 keeps NAT sessions alive; ping_timeout=60 drops ghost connections.
    # compression=None disables per-message deflate for raw binary speed.
    async with websockets.serve(
        handle_connection,
        host="0.0.0.0",
        port=PORT,
        ping_interval=20,
        ping_timeout=60,
        max_size=PACKET_SIZE * 10,  # Reject suspiciously large messages early
        compression=None,
    ):
        log.info("  Server ready. Waiting for connections...")
        await asyncio.Future()  # Run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("  Server shut down by operator (Ctrl+C).")
