<p align="center">
  <img src="docs/banner.png" alt="Pokémon ORAS MMO" width="750"/>
</p>

<h1 align="center">Pokémon ORAS MMO</h1>

<p align="center">
  <b>Play Pokémon Omega Ruby / Alpha Sapphire together: see your friends walking in your own world, in real time.</b><br/>
  A 3GX plugin for the <a href="https://azahar-emu.org">Azahar</a> emulator (PC and Android) + a tiny open-source relay server you host yourself.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-17.0-blue" alt="version 17.0"/>
  <img src="https://img.shields.io/badge/platform-Azahar%20PC%20%7C%20Android-green" alt="Azahar PC and Android"/>
  <img src="https://img.shields.io/badge/server-Python%203.8%2B-yellow" alt="Python 3.8+"/>
  <img src="https://img.shields.io/badge/license-MIT-lightgrey" alt="MIT"/>
</p>

<p align="center"><a href="README_ES.md">🇪🇸 Leer en español</a></p>

---

## Contents

1. [What it does](#what-it-does)
2. [How it works](#how-it-works)
3. [Quick start (players)](#quick-start-players)
4. [The SELECT menu](#the-select-menu)
5. [Host your own server](#host-your-own-server)
6. [Play online without a rented server](#play-online-without-a-rented-server) (ZeroTier, Tailscale, Radmin, router)
7. [Configuration files](#configuration-files)
8. [Troubleshooting](#troubleshooting)
9. [Build from source](#build-from-source)
10. [Make it yours (developers)](#make-it-yours-developers) — see also [CONTRIBUTING](CONTRIBUTING.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
11. [Security and privacy](#security-and-privacy)
12. [FAQ](#faq)
13. [Project layout](#project-layout)
14. [Legal](#legal)

---

## What it does

- **Your friends inside your game**: up to 12 players per room. Each one appears on your map with the game's
  **real 3D trainer model** (Brendan / May), walking, running and cycling (Mach or Acro bike), smoothly even
  with a bad connection (per-player jitter buffer and prediction).
- **3D Pokémon followers**: your lead Pokémon (and your friends') follows its trainer, rendered from the game's own
  3D model in 8 directions with a real walk cycle. It walks only on your footsteps or on free ground, so it never
  goes through houses, hedges or trees, and buildings hide it correctly when it walks behind them.
  *(Needs the optional `mmo_pokes_3d.bin` pack, see [FAQ](#faq).)*
- **Native maps**: friends are shown as pins on the game's own **AreaNav** and **Fly map**, at their exact spot.
- **SELECT menu** (mouse, touch or buttons): players online, chat with quick messages and whispers, emotes
  (`!`, happy, heart, laugh...), battle and trade invitations, your character (16 outfit colors + hat/bow color,
  name tag color), display options, connection, help.
- **Persistent ranking per room**: badges, League, Pokédex and team, saved by the server (also for offline players).
- **Real battles and trades**: invitations from the MMO menu, played through Azahar's own multiplayer room (PSS).
- **7 languages**, for the MMO menu **and the whole game**: Spanish, English, French, Italian, German, Japanese
  (kana or kanji) and Korean. Default = the language of your save; change it in SELECT → Display → Language.
  Official Pokémon and location names are read from your own game; Japanese/Korean use the game's own font.
- **Cross-play**: Azahar on Windows/Linux/macOS and Azahar on Android play in the same rooms.
- **Your save is never modified.** The plugin reads the game's memory and only writes the few visual things it
  needs (your outfit color, friends' models, hiding the game's own head icon on the map, the game language at boot).

## How it works

```
 Azahar (PC / Android)                                          Your server (PC, VPS, Docker...)
┌──────────────────────────────────────────┐   TCP, 64-byte    ┌─────────────────────────────────┐
│ Pokémon Omega Ruby / Alpha Sapphire      │   packets         │ server/server.py (Python 3.8+,  │
│  └─ OrasMmo.3gx (CTRPluginFramework)     │ ◄───────────────► │ no dependencies)                │
│     · reads your position, map, camera   │   room code +     │  · rooms of up to 12 players    │
│     · draws friends, followers, menus    │   player data     │  · relays positions, chat, etc. │
│     · writes mmo_*.txt next to itself    │                   │  · saves the ranking per room   │
└──────────────────────────────────────────┘                   └─────────────────────────────────┘
          │  (optional) Azahar multiplayer room, UDP 24872 — real battles and trades (PSS)
```

The server is a **relay**: it never sees your save, only what the plugin sends (nickname, model, position,
chat, emotes and ranking stats). Anyone can host one; there is no central server.

## Quick start (players)

You need: **[Azahar](https://azahar-emu.org)** (latest), **your own legal dump** of Pokémon Omega Ruby
(`000400000011C400`) or Alpha Sapphire (`000400000011C500`) already working in Azahar (Azahar's website explains how to
dump your own game), and the address of a server (yours or a friend's).

### PC (Windows)
1. Download `ORAS_MMO_v17.0_Global.zip` from [Releases](../../releases) and extract it.
2. Double-click **`INSTALL_PC.bat`**. It finds Azahar, installs the plugin for both games and asks for the server
   address (`IP` or `IP:port`; leave it empty to use `127.0.0.1`, a server on the same PC).
3. In Azahar: **Emulation → Configure → System** (or **Debug**) → enable the **3GX plugin loader** (once).
4. Start the game and load your save. Press **SELECT** to open the MMO menu.

### PC (Linux / macOS) and manual install
Open Azahar's user folder (**File → Open Azahar Folder**; usually `%APPDATA%\Azahar` on Windows,
`~/.local/share/azahar-emu` on Linux, `~/Library/Application Support/Azahar` on macOS).
Copy `OrasMmo.3gx` to `<Azahar user folder>/sdmc/luma/plugins/000400000011C400/` (Omega Ruby) and/or
`.../000400000011C500/` (Alpha Sapphire). To set the server, create `mmo_server.txt` in the same folder with the IP
(e.g. `203.0.113.10` or `203.0.113.10:9000`).

### Android
1. With a file manager, open Azahar's data folder → `sdmc/luma/plugins/` (create `luma` and `plugins` if missing).
2. Copy `OrasMmo.3gx` there **renamed to `default.3gx`**, plus `mmo_server.txt` with your server's IP.
3. Azahar → Settings → System: enable **"3GX plugin loader"**; Graphics: **OpenGL ES**.
4. Start the game; tap **SELECT** to open the menu (the bottom screen is touch-enabled).

### Play together
- Everyone uses the **same server** and the **same room code** (SELECT → Connection, default `1234`).
  Use your own, hard-to-guess room code: it is the only thing that keeps strangers out of your room.
- No server? One of you can host it at home: see
  [Play online without a rented server](#play-online-without-a-rented-server).
- **Battles / trades**: send the invitation from SELECT → Battles, then both join the same Azahar room
  (**Multiplayer → Direct connect**) and open the **PSS** on the touch screen.

## The SELECT menu

Press **SELECT** in the game (Android: the on-screen SELECT button). Mouse, touch screen and buttons all work.

| Section | What you do there |
|---|---|
| **Home** | Connection status, your room, where you are; shortcuts: who's playing, greet everyone, send a message, invite, change colors |
| **Players** | Everyone in your room and where they are; pick someone to whisper or invite |
| **Ranking** | The room's table: badges, League, Pokédex, team (offline players included) |
| **Chat & emotes** | Quick messages, free text, whispers and emotes (`!`, happy, heart, laugh...) |
| **Battles** | Invite to battle or trade; watch a friend's battle live |
| **My character** | Outfit color (16), hat/bow color, name tag color, preview |
| **Display** | Status box, names, followers (all / mine / none), performance (auto / quality / saver), **language** |
| **Connection** | Your name, room code, server, reconnect |
| **Help** | Short guides inside the game |

## Host your own server

Full guide: **[server/README.md](server/README.md)** (Windows, Linux VPS, Docker, systemd, firewall, limits).

```bash
cd server
python3 server.py                 # listens on TCP 9000 — no dependencies
PORT=9100 python3 server.py       # another port (players: mmo_server.txt = "IP:9100")
docker compose up -d              # or with Docker (copy .env.example to .env first)
```

Open **TCP 9000** (and **UDP 24872** if you also run the Azahar room) on your router or hosting panel.

## Play online without a rented server

You don't need a VPS. One player (the **host**) runs the server on their own PC, and friends anywhere in the world
reach that PC through a free **virtual network** (ZeroTier, Tailscale, Radmin VPN...) or through an open router port.
The host's PC must stay on while you play.

### 1. The host starts the server
1. Install [Python 3](https://www.python.org/downloads/) (Windows: tick **"Add Python to PATH"**).
2. Double-click `server/run_server.bat` (Linux/macOS: `python3 server/server.py`). Leave the window open.
3. Windows asks to allow Python through the firewall: allow it on **private and public networks** (virtual networks
   are often marked as "public"). Or add the rule yourself, in a terminal opened as administrator:
   ```bat
   netsh advfirewall firewall add rule name="ORAS MMO" dir=in action=allow protocol=TCP localport=9000
   ```
4. The host plays on the same PC with the default server `127.0.0.1` (nothing to change).

### 2. Connect your friends (pick one)

**ZeroTier** (free; Windows, macOS, Linux, **Android**) — what this project used before it had a VPS:
1. The host creates an account at [my.zerotier.com](https://my.zerotier.com), clicks **Create A Network** and copies
   the **Network ID** (16 characters). Leave **Access Control** on **Private**.
2. Everyone (host included) installs [ZeroTier One](https://www.zerotier.com/download/) and **joins** that Network ID
   (Windows: tray icon → *Join New Network*; Android: **+**).
3. On the network page, under **Members**, the host ticks **Auth** for each friend — only people you know.
4. The host's **Managed IP** in that list (e.g. `10.147.17.5`) is the server address for everyone else.

**Tailscale** (free for personal use; Windows, macOS, Linux, **Android**):
1. Everyone installs [Tailscale](https://tailscale.com/download) and signs in.
2. The host shares their PC with each friend (admin console → the machine → **Share**) or invites them to the tailnet.
3. The host's **Tailscale IP** (`100.x.y.z`, shown in the app) is the server address.

**Radmin VPN** (free, no account, **Windows only** — no Android): the host creates a network with a name and
password, friends join it, and the host's `26.x.x.x` IP is the server address.

**Open a router port** (no extra program): forward **TCP 9000** on the host's router to the host PC's local IP, and
give friends your **public IP** (search "what is my IP"). Downsides: friends learn your home IP, it can change, and it
does not work if your provider uses CGNAT — then use a virtual network instead.

### 3. Every friend sets the host's address
SELECT → **Connection → Server** → type the host's IP (virtual network IP or public IP), or put it in
`mmo_server.txt` next to the plugin (Windows: `INSTALL_PC.bat` asks for it). Everyone uses the **same room code**.

### 4. Battles and trades (optional)
The host opens an Azahar room: **Multiplayer → Host Room** (port **24872**, set a password). Friends use
**Multiplayer → Direct Connect** with the host's same IP and port 24872. With a router port instead of a virtual
network, also forward **UDP 24872**. Windows firewall rule for it:
`netsh advfirewall firewall add rule name="Azahar room" dir=in action=allow protocol=UDP localport=24872`

**If someone can't connect:** the server window shows a line for every player who joins; check the firewall rule,
that ZeroTier/Tailscale says *connected* on both sides (and the friend is authorized), the IP, and the room code.
Virtual networks encrypt the traffic between you; only add people you trust to yours.

## Configuration files

All of them live next to the plugin (`sdmc/luma/plugins/<TitleID>/`, or `plugins/` on Android):

| File | What it is | Example |
|---|---|---|
| `mmo_server.txt` | Server address: IPv4, optionally `:port` (no domain names) | `203.0.113.10:9000` |
| `mmo_room.txt` | Room code (1–16 characters) | `MyCrew2026` |
| `mmo_name.txt` | Your nickname (up to 13 characters) | `Ash` |
| `mmo_settings.txt` | Everything you set in the SELECT menu | written by the plugin |
| `mmo_pokes_3d.bin` | 3D followers pack (optional) | see [FAQ](#faq) |
| `mmo_log.txt` | Log: attach it to bug reports | written by the plugin |

Most players never touch these files: the SELECT menu edits them for you.

## Troubleshooting

| Problem | Try this |
|---|---|
| Nothing happens when I press SELECT | Enable Azahar's 3GX plugin loader; check the plugin is in `sdmc/luma/plugins/000400000011C400/` (Omega Ruby) or `.../000400000011C500/` (Alpha Sapphire), or `plugins/default.3gx` on Android; restart the game |
| I don't see my friends | Same server **and** same room code for everyone; the server window must be open; SELECT → Connection → Reconnect; check the host's firewall |
| "Disconnected" in the menu | Wrong IP or port, the server is off, or a firewall blocks TCP 9000 |
| The game runs slowly | SELECT → Display → Performance → **Saver**; on Android use **OpenGL ES** |
| No Pokémon follow us | You need `mmo_pokes_3d.bin` (see FAQ) |
| Reporting a bug | Attach `mmo_log.txt` (next to the plugin) — remove your server IP and room code first |

## Build from source

Requirements ([devkitPro](https://devkitpro.org/wiki/Getting_Started)): **devkitARM** (r68), **libctru** (2.7),
**libctrpf** 0.8 (CTRPluginFramework) and **3gxtool** 1.3. libctrpf and 3gxtool come from the
[ThePixellizerOSS package repository](https://thepixellizeross.gitlab.io/packages/).

```bash
cd 3ds_plugin
make                      # -> 3ds_plugin.3gx  (rename it to OrasMmo.3gx)
```

- **Windows**: devkitPro's `make` breaks with spaces in the path; build from a folder like `C:\dev\ORAS-MMO`.
- Flags: `-Os -std=gnu++11 -fno-rtti -fno-exceptions`; plugin memory `10MiB` (`CTRPluginFramework.plgInfo`).
- Translations live in **`3ds_plugin/tools/i18n/strings.py`**; after editing, run
  `python 3ds_plugin/tools/i18n/gen_lang.py` (it regenerates `Includes/LangTable.inc` and checks `%s`/`%d`).
- Server tests: `python -m unittest server/test_server_ranking.py server/test_server_limits.py`.
- Release ZIP: `python tools/package_public.py path/to/OrasMmo.3gx` → `dist/ORAS_MMO_v17.0_Global.zip`.

## Make it yours (developers)

- **Default server for your community**: copy `3ds_plugin/Includes/ServerConfig.example.hpp` to
  `ServerConfig.local.hpp` and set your IP, port and room. That file is **git-ignored**: your build connects
  automatically, and your address never ends up in a public repository. Without it the plugin defaults to
  `127.0.0.1:9000`, room `1234`.
- **Private server**: `ORAS_ROOMS=MyRoom,OtherRoom` makes the server accept only those rooms.
- Code comments cite `KB §x.y`: the project's reverse-engineering notes (game addresses and structures), which are not
  part of this repository. The values themselves are in the code.
- **Network protocol**: 64-byte packets, documented in **[docs/PROTOCOL.md](docs/PROTOCOL.md)** — write your own
  server or tools against it.
- **Before publishing a fork**: `python tools/prepublish_check.py` scans what git would publish for your private
  data (IPs, names, passwords listed in a git-ignored `.private_patterns.txt`) and for files that must never be
  shared (`mmo_pokes_*.bin`, ZIPs, rankings, `.env`).

## Security and privacy

- **No accounts, no passwords, no telemetry.** The plugin only talks to the server you configure.
- What the server receives: nickname, trainer model, position/map, chat and emotes, invitations and ranking stats
  (badges, Pokédex, play time, team levels). It also sees your IP address, like any online game server.
- The server validates the room code, overwrites the sender id (no impersonating player slots), limits every
  client's queue and enforces connection limits (total, per IP, rooms, ranking files). Ranking files are named by
  the hex of the room code, so a room code can never write outside the server folder.
- Each player can send at most 120 packets per second (the plugin sends about 30); a flood is dropped, never relayed.
- The plugin bounds-checks everything it receives (player ids, text fields are always cut and terminated) and every
  file it reads (`mmo_*.txt`, the followers pack).
- **There is no authentication**: anyone who knows your server address and room code can join that room and
  pick any nickname. Use a private room code (or `ORAS_ROOMS`) and only share it with people you trust.
- Found a vulnerability? Please open a private
  [security advisory](../../security/advisories/new) instead of a public issue.

## FAQ

**Followers don't appear.** They need `mmo_pokes_3d.bin`, a pack rendered from the game's own 3D models. It is
not distributed here (it contains game graphics). Without it everything else works; followers are just not drawn.
Build it yourself from your own copy of the game with [`tools/pokepack`](tools/pokepack/README.md).

**Japanese or Korean text?** Pick it in SELECT → Display → Language. The MMO menu changes at once; the game's own
texts change after restarting the game. Your save is not modified.

**"Server: Automatic" in the menu?** It means the default server compiled into the plugin (`127.0.0.1` in the
public build). Set yours in SELECT → Connection, or with `mmo_server.txt`.

**Real 3DS?** It is built and tested on Azahar. A real console with Luma3DS's plugin loader may work, but it is
untested.

**Another game version?** The plugin checks the game build at boot. If it does not match, it disables the
features that touch the game's memory instead of crashing.

## Project layout

```
3ds_plugin/            The 3GX plugin (C++ / CTRPluginFramework)
  Sources/, Includes/  Game reading, rendering, menu, network, languages
  tools/i18n/          Translations (strings.py) and table generator
server/                Relay server (server.py), tests, Docker files, hosting guide
packaging/             Files that go into the release ZIP (installer, quick start)
extras/shaders/        Optional post-processing shader for Azahar (OpenGL)
tools/                 package_public.py (release ZIP), prepublish_check.py, fake_players.py (test bots)
  pokepack/            Builds the 3D followers pack from your own game
docs/                  ARCHITECTURE.md (developer guide), PROTOCOL.md, banner
CONTRIBUTING.md        How to contribute · SECURITY.md: how to report vulnerabilities
```

## Legal

Pokémon ORAS MMO is a non-commercial fan project, not affiliated with or endorsed by Nintendo, Creatures Inc. or
GAME FREAK inc. Pokémon and all related names are trademarks of their respective owners. This repository contains
**no ROMs, no game code and no game assets**: names, fonts, models and textures are read at runtime from **your own
copy** of the game. Use it only with games you legally own.

Code under the [MIT License](LICENSE). Created by [SamuTecYT](https://github.com/SamuTecYT) and contributors.
