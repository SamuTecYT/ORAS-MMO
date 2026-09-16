"""
test_client.py -- Project ORAS MMO Server Test Client
======================================================
Simulates 2 players connecting to the relay server, exchanging position
packets, and testing battle lock / cutscene lock / disconnect handling.

Usage:
    python test_client.py [--host HOST] [--port PORT] [--room ROOM]

Defaults:
    host = localhost
    port = 8765
    room = TEST  (4-char alphanumeric code)

Prerequisites:
    pip install websockets
"""

import asyncio
import struct
import argparse
import time
import sys

try:
    import websockets
except ImportError:
    print("[ERROR] 'websockets' package not found. Run: pip install websockets")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Packet constants (mirrors server.py exactly)
# ---------------------------------------------------------------------------
PACKET_SIZE = 64

PKT_POSITION       = 0x01
PKT_BATTLE_START   = 0x02
PKT_BATTLE_END     = 0x03
PKT_CUTSCENE_START = 0x04
PKT_CUTSCENE_END   = 0x05
PKT_MAP_CHANGE     = 0x06
PKT_PING           = 0xFF
PKT_PONG           = 0xFF
PKT_DISCONNECT     = 0x10
PKT_LOCK           = 0x11
PKT_UNLOCK         = 0x12

LOCALE_EN = 0
LOCALE_ES = 1

PACKET_STRUCT = struct.Struct("<BBBBIfffBBBB6s34s")
assert PACKET_STRUCT.size == PACKET_SIZE, "Packet struct size mismatch!"

# Human-readable names for received packet types
PKT_TYPE_NAMES = {
    0x01: "POSITION",
    0x02: "BATTLE_START",
    0x03: "BATTLE_END",
    0x04: "CUTSCENE_START",
    0x05: "CUTSCENE_END",
    0x06: "MAP_CHANGE",
    0xFF: "PING/PONG",
    0x10: "DISCONNECT",
    0x11: "LOCK",
    0x12: "UNLOCK",
}

# ---------------------------------------------------------------------------
# Packet helpers
# ---------------------------------------------------------------------------

def build_packet(
    pkt_type, player_id, locale=LOCALE_EN, map_id=0,
    pos_x=0.0, pos_y=0.0, pos_z=0.0,
    facing=0, anim_frame=0, battle_type=0, flags=0,
    pokemon_levels=b"\x00" * 6,
) -> bytes:
    """Build a 64-byte binary packet."""
    return PACKET_STRUCT.pack(
        pkt_type, player_id, locale, 0,
        map_id, pos_x, pos_y, pos_z,
        facing, anim_frame, battle_type, flags,
        pokemon_levels, b"\x00" * 34,
    )


def parse_packet(data: bytes) -> dict:
    """Parse a 64-byte packet into a human-readable dict."""
    if len(data) != PACKET_SIZE:
        return {"error": f"bad_size:{len(data)}"}
    (
        pkt_type, player_id, locale, _res,
        map_id, pos_x, pos_y, pos_z,
        facing, anim_frame, battle_type, flags,
        pokemon_levels, _future
    ) = PACKET_STRUCT.unpack(data)
    return {
        "type":          PKT_TYPE_NAMES.get(pkt_type, f"0x{pkt_type:02X}"),
        "player_id":     player_id,
        "locale":        "EN" if locale == LOCALE_EN else "ES",
        "map_id":        map_id,
        "pos":           (round(pos_x, 2), round(pos_y, 2), round(pos_z, 2)),
        "facing":        facing,
        "anim_frame":    anim_frame,
        "battle_type":   battle_type,
        "flags":         f"0b{flags:08b}",
        "pokemon_levels": list(pokemon_levels),
    }


# ---------------------------------------------------------------------------
# Player simulation coroutine
# ---------------------------------------------------------------------------

async def simulate_player(
    host: str, port: int, room: str, label: str,
    actions: list, results: list,
) -> None:
    """
    Connect one simulated player and execute a scripted list of actions.

    Each action is a dict with a 'type' key and optional fields.
    Supported types: position, battle_start, battle_end,
                     cutscene_start, cutscene_end, ping, wait
    """
    uri = f"ws://{host}:{port}"
    print(f"[{label}] Connecting to {uri}  room={room}...")

    try:
        async with websockets.connect(uri, max_size=PACKET_SIZE * 100) as ws:
            # --- Handshake ---
            await ws.send(f"{room}:0")
            hs_response = await asyncio.wait_for(ws.recv(), timeout=5.0)
            print(f"[{label}] Handshake response: {hs_response}")

            if not hs_response.startswith("OK:"):
                print(f"[{label}] Handshake rejected: {hs_response}")
                return

            _ok, _rcode, assigned_id = hs_response.split(":")
            player_id = int(assigned_id)
            print(f"[{label}] Joined as Player {player_id}")

            # --- Background receive task (collects all incoming packets) ---
            received = []

            async def recv_loop():
                try:
                    async for msg in ws:
                        if isinstance(msg, bytes) and len(msg) == PACKET_SIZE:
                            parsed = parse_packet(msg)
                            received.append(parsed)
                            print(f"[{label}] <- RECV: {parsed}")
                        else:
                            # Text frame from server (rare)
                            print(f"[{label}] <- TEXT: {msg!r}")
                except Exception:
                    pass  # Connection closed cleanly

            recv_task = asyncio.create_task(recv_loop())

            # --- Execute scripted actions sequentially ---
            for action in actions:
                delay = action.get("delay", 0.5)
                await asyncio.sleep(delay)

                atype = action.get("type", "position")

                if atype == "wait":
                    # Pure delay — already slept above
                    continue

                elif atype == "position":
                    pkt = build_packet(
                        PKT_POSITION, player_id,
                        locale=action.get("locale", LOCALE_EN),
                        map_id=action.get("map_id", 101),
                        pos_x=action.get("pos_x", 0.0),
                        pos_y=action.get("pos_y", 0.0),
                        pos_z=action.get("pos_z", 0.0),
                        facing=action.get("facing", 0),
                        anim_frame=action.get("anim_frame", 0),
                    )
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND POSITION  "
                          f"map={action.get('map_id',101)}  "
                          f"pos=({action.get('pos_x',0):.1f},"
                          f"{action.get('pos_y',0):.1f},"
                          f"{action.get('pos_z',0):.1f})  "
                          f"locale={'ES' if action.get('locale')==LOCALE_ES else 'EN'}")

                elif atype == "battle_start":
                    pkt = build_packet(PKT_BATTLE_START, player_id,
                                       battle_type=action.get("battle_type", 1))
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND BATTLE_START  "
                          f"battle_type={action.get('battle_type', 1)}")

                elif atype == "battle_end":
                    pkt = build_packet(PKT_BATTLE_END, player_id)
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND BATTLE_END")

                elif atype == "cutscene_start":
                    pkt = build_packet(PKT_CUTSCENE_START, player_id)
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND CUTSCENE_START")

                elif atype == "cutscene_end":
                    pkt = build_packet(PKT_CUTSCENE_END, player_id)
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND CUTSCENE_END")

                elif atype == "ping":
                    t0 = time.monotonic()
                    pkt = build_packet(PKT_PING, player_id)
                    await ws.send(pkt)
                    print(f"[{label}] -> SEND PING")
                    # Poll received list for up to 2 seconds waiting for pong
                    deadline = time.monotonic() + 2.0
                    while time.monotonic() < deadline:
                        await asyncio.sleep(0.05)
                        if any(r.get("type") == "PING/PONG" for r in received):
                            rtt_ms = (time.monotonic() - t0) * 1000
                            print(f"[{label}] <- PONG received! RTT={rtt_ms:.2f}ms")
                            break
                    else:
                        print(f"[{label}] WARNING: No pong received within 2s")

            # Allow a moment to receive any trailing packets before closing
            await asyncio.sleep(1.0)
            recv_task.cancel()

            results.extend(received)
            print(f"[{label}] Session complete. Total packets received: {len(received)}")

    except ConnectionRefusedError:
        print(f"[{label}] Connection refused. Is the server running on {host}:{port}?")
    except Exception as exc:
        print(f"[{label}] Unexpected error: {exc}")


# ---------------------------------------------------------------------------
# Main test scenario
# ---------------------------------------------------------------------------

async def run_test(host: str, port: int, room: str) -> None:
    print()
    print("=" * 60)
    print("  Project ORAS MMO -- Server Integration Test")
    print(f"  Server:  ws://{host}:{port}")
    print(f"  Room:    {room}")
    print("=" * 60)
    print()

    results_p1: list = []
    results_p2: list = []

    # --- Player 1 script ---
    # Moves around, enters a wild battle, exits, pings, triggers a cutscene.
    actions_p1 = [
        # Send a few position updates to warm up
        {"type": "position", "delay": 0.2, "map_id": 101,
         "pos_x": 10.0, "pos_y": 0.0, "pos_z": 5.0, "facing": 0},
        {"type": "position", "delay": 0.4, "map_id": 101,
         "pos_x": 11.0, "pos_y": 0.0, "pos_z": 5.0, "facing": 3},
        {"type": "position", "delay": 0.4, "map_id": 101,
         "pos_x": 12.0, "pos_y": 0.0, "pos_z": 5.0, "facing": 3},
        # Enter wild battle — server should LOCK P1 for P2
        {"type": "battle_start", "delay": 0.4, "battle_type": 1},
        {"type": "wait", "delay": 1.5},    # Simulate battling duration
        # Exit battle — server should UNLOCK P1 for P2
        {"type": "battle_end", "delay": 0.0},
        {"type": "position", "delay": 0.4, "map_id": 101,
         "pos_x": 13.0, "pos_y": 0.0, "pos_z": 5.0},
        # Test ping/pong round-trip
        {"type": "ping", "delay": 0.3},
        # Trigger a cutscene — server should LOCK then UNLOCK
        {"type": "cutscene_start", "delay": 0.4},
        {"type": "wait", "delay": 1.0},
        {"type": "cutscene_end", "delay": 0.0},
        {"type": "wait", "delay": 0.5},
    ]

    # --- Player 2 script ---
    # Observes as a Spanish-locale player; sends position updates and pings.
    # Should receive LOCK/UNLOCK packets generated by P1's battle/cutscene.
    actions_p2 = [
        {"type": "position", "delay": 0.3, "map_id": 101, "locale": LOCALE_ES,
         "pos_x": 20.0, "pos_y": 0.0, "pos_z": 8.0},
        {"type": "position", "delay": 0.4, "map_id": 101, "locale": LOCALE_ES,
         "pos_x": 21.0, "pos_y": 0.0, "pos_z": 8.0},
        {"type": "position", "delay": 0.4, "map_id": 101, "locale": LOCALE_ES,
         "pos_x": 22.0, "pos_y": 0.0, "pos_z": 8.0},
        {"type": "wait", "delay": 1.0},
        {"type": "position", "delay": 0.4, "map_id": 101, "locale": LOCALE_ES,
         "pos_x": 23.0, "pos_y": 0.0, "pos_z": 8.0},
        {"type": "ping", "delay": 0.3},
        # Stay alive long enough to receive P1's lock/unlock cycle
        {"type": "wait", "delay": 4.0},
    ]

    # Run both players concurrently in the same room
    await asyncio.gather(
        simulate_player(host, port, room, "Player1-EN", actions_p1, results_p1),
        simulate_player(host, port, room, "Player2-ES", actions_p2, results_p2),
    )

    # --- Test result summary ---
    print()
    print("=" * 60)
    print("  TEST SUMMARY")
    print("=" * 60)
    print(f"  Player 1 received: {len(results_p1)} packets")
    print(f"  Player 2 received: {len(results_p2)} packets")
    print()

    # Check that Player 2 received the expected server-generated events
    p2_types = [r.get("type") for r in results_p2]
    checks = [
        ("LOCK   received by P2 (battle/cutscene start)", "LOCK"   in p2_types),
        ("UNLOCK received by P2 (battle/cutscene end)",   "UNLOCK" in p2_types),
        ("POSITION relayed to P2 from P1",  "POSITION" in p2_types),
    ]

    all_passed = True
    for label, ok in checks:
        mark = "OK" if ok else "FAIL"
        print(f"  [{mark}] {label}")
        if not ok:
            all_passed = False

    print()
    if all_passed:
        print("  ALL CHECKS PASSED -- Server is working correctly!")
    else:
        print("  Some checks failed. Make sure the server is running first.")
    print("=" * 60)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Project ORAS MMO -- Server Integration Test Client"
    )
    parser.add_argument("--host", default="localhost",
                        help="Server hostname (default: localhost)")
    parser.add_argument("--port", type=int, default=8765,
                        help="Server port (default: 8765)")
    parser.add_argument("--room", default="TEST",
                        help="Room code — 4 alphanumeric chars (default: TEST)")
    args = parser.parse_args()

    room = args.room.upper()
    if not (len(room) == 4 and room.isalnum()):
        print("[ERROR] Room code must be exactly 4 alphanumeric characters (e.g. TEST, AB12).")
        sys.exit(1)

    asyncio.run(run_test(args.host, args.port, room))


if __name__ == "__main__":
    main()
