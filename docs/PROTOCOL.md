# 📡 ORAS MMO network protocol (v16, used by plugin 17.0)

← [Back to the README](../README.md) · [Contributing](../CONTRIBUTING.md) · [Architecture](ARCHITECTURE.md) · [Protocol](PROTOCOL.md)

Plain TCP, little-endian, no encryption (the server only relays game-overlay data; see the security notes in the
README). Reference implementations: `server/server.py` and `3ds_plugin/Includes/Protocol.hpp`.

## 🤝 1. Handshake

1. The client connects to `IP:port` (default port `9000`) and sends the **room code** as ASCII, 1–16 printable
   characters (`0x20`–`0x7E`), no terminator.
2. The server answers one line:
   - `OK:ROOM:<room>:KEY:<id>\n` — `<id>` is your player slot, `1..12`. `KEY:` means the server keeps the ranking
     with player keys ([section 4](#ranking-keys)). Older servers send `OK:ROOM:<room>:<id>\n`: read the id after
     the last `:`.
   - `ERROR:ROOM_FULL\n` — the room (or the server's room limit) is full, or it already has 4 players from your IP.
   - `ERROR:ROOM_DENIED\n` — the server only accepts other rooms (`ORAS_ROOMS`), or your IP tried too many different
     room codes (5 in 10 minutes). Both cases get the same answer, so it doesn't tell whether a room exists.
   - The connection is simply closed if a connection limit is reached or the code is invalid.
3. From then on both sides exchange **fixed 64-byte packets**. Bytes received right after the `\n` already belong to
   the packet stream.

The server overwrites `player_id` of every packet with the sender's real slot and relays each packet to everyone else
in the room (except whispers, see CHAT, and `KEY`, which only the server reads). Only the types a client sends are
relayed: `LEAVE`, `RANK_SNAPSHOT` and `NAME_TAKEN` coming from a client are dropped. A slot is freed after 60 s
without a full packet: send a `PING` at least every few seconds while idle.

## 📦 2. Packet layout (64 bytes)

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 1 | `type` | packet type (table below) |
| 1 | 1 | `player_id` | sender slot 1..12 (set by the server) |
| 2 | 2 | `model_id` | 172 Brendan, 171 May |
| 4 | 4 | `map_id` | bits 0–15 area, bits 16–31 zone |
| 8 | 4 | `x` | float, east–west (world units, 18 = one tile) |
| 12 | 4 | `y` | float, north–south |
| 16 | 4 | `z` | float, height |
| 20 | 1 | `facing` | 0 S, 1 N, 2 W, 3 E |
| 21 | 1 | `anim` | POSITION: move state · CHAT: quick-message id (255 = free text) · EMOTE: id · EVENT_*: target slot |
| 22 | 1 | `btype` | POSITION: hat/bow color (0 = like outfit, 1 = original, 2..16) · CHAT: whisper target (0 = everyone) · EVENT_*: 1 battle / 2 trade (RESP: 1 accept / 2 decline) |
| 23 | 1 | `flags` | bit 0 in battle, 1 map transition, 2 hidden, 3 Acro bike, 4 in a menu, 7 offline (snapshot) |
| 24 | 2 | `lvls[0..1]` | yaw (u16) |
| 26 | 1 | `lvls[2]` | protocol version (16) |
| 27 | 1 | `lvls[3]` | low nibble outfit color 0..15, high nibble name-tag color 0..15 |
| 28 | 2 | `lvls[4..5]` | sequence number (u16) |
| 30 | 14 | `nickname` | ASCII, NUL-padded (13 chars max) |
| 44 | 20 | `msg` | depends on the type (below) |

Move states (`anim` in POSITION): 0 idle, 1 walk, 2 run, 3 bike, 4 surf.

## 🏷️ 3. Packet types

| Type | Name | `msg` contents |
|---|---|---|
| `0x01` | POSITION | `[0..3]` sender clock in ms (u32), used for interpolation |
| `0x02` | EMOTE | — (`anim` = emote id) |
| `0x03` | CHAT | free text when `anim` = 255 (NUL-padded) |
| `0x20` | EVENT_REQ | — battle/trade invitation to slot `anim` |
| `0x21` | EVENT_RESP | — answer to an invitation |
| `0x22` | EVENT_END | — invitation finished |
| `0x23` | LEAVE | sent by the server when a player disconnects |
| `0x31` | STATS | 18-byte ranking stats (below) |
| `0x32` | BATTLE | live battle: kind, my species u16, level, HP u16, max HP u16, foe species u16, level, HP u16, max HP u16 |
| `0x33` | RANK_SNAPSHOT | sent by the server on join, one per saved player: 18-byte stats + days since last seen (u16) |
| `0x34` | KEY | client → server only, right after the handshake: `[0..15]` your ranking key for this server and room ([section 4](#ranking-keys)); never relayed |
| `0x35` | NAME_TAKEN | server → that client only: its nickname (in `nickname`) belongs to another player |
| `0xFF` | PING | same as POSITION `msg` |

STATS (`msg[0..17]`): play hours u16, minutes u8, badges bitmask u8, caught u16, seen u16, money u32, party count u8,
average level u8, max level u8, lead species u16, validity flags u8 (bit 0 time, 1 Pokédex, 2 party, 3 misc,
4 League champion). The server stores the last STATS of each nickname per room (`ranking_<hex room>.json`).

<a id="ranking-keys"></a>
## 🔑 4. Ranking keys (plugin 17.0 r4)

There are no accounts, so the ranking used to trust the nickname alone: anyone could use your nickname to change or
delete your stats. Now each player has a key:

- The plugin creates a random 16-byte key once and keeps it in `mmo_key.txt`, next to the plugin. The key itself never
  leaves the device: after `OK:ROOM:<room>:KEY:<id>` the plugin sends one `KEY` packet with
  `SHA-256("ORASMMO-KEY1|" + key + "|<server ip>:<port>|<room>")[0..15]`, so every server and room gets a different
  value. With an older server (no `KEY:`) it sends nothing.
- The server only stores `SHA-256("oras-rank-key|" + that value)`, as the `owner` of the nickname's entry.
- The first player who saves stats with a nickname owns it. Anyone else using it (another key, or an older plugin
  with no key) can't change or delete those stats: their STATS are dropped, the others see them as `nickname*`, and
  a client that sent a key gets one `NAME_TAKEN`. Entries saved before keys existed go to the first key that saves
  them.
- A nickname nobody has played for `ORAS_RANK_CLAIM_DAYS` days (30) can be taken again, and each IP can create at most
  `ORAS_RANK_NEW_PER_IP` (5) new ranking nicknames per day.
- To play with the same nickname on another device, copy `mmo_key.txt` there. Plain TCP: whoever can read your
  traffic could copy that value too, so the key protects the ranking from other players, not from your network.
