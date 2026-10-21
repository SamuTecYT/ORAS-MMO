# ORAS MMO — architecture and developer guide

A map of the plugin for anyone who wants to improve it. Code comments are mostly in Spanish; identifiers and this
guide are in English. Comments that cite `KB §x.y` refer to the project's reverse-engineering notes (not published):
the addresses and structures they describe are written in the code itself.

## 1. Big picture

```
Azahar ─ Pokémon OR/AS ─┬─ game thread ── GameHook (hook on the game's per-frame update): friends as REAL game models
                        │                 DepthProbe (hook on the GPU command queue, read-only): scene depth
                        │                 GameLang (hook on SetLanguage, at boot): game language
                        ├─ OSD callback ─ Overlay::OnFrame: everything drawn over the game, every frame
                        ├─ network thread ─ NetworkClient / Players: TCP to server.py, 64-byte packets
                        ├─ asset thread ── HeroAssets, GameText, GameFont, Follower3D: loads data from the game's
                        │                  RomFS and from mmo_pokes_3d.bin, never on the game thread
                        └─ SELECT menu ─── Ui + UiGfx (own loop; the game is paused while it is open)
```

Entry point: `Sources/main.cpp` — `PatchProcess()` runs before the game's `main` (installs GameLang), `main()` loads
the config, installs the hooks, starts the threads and registers the OSD callback.

## 2. Files (`3ds_plugin/Sources` + `Includes`)

| File | Role |
|---|---|
| `main.cpp` | Boot, network thread (sends POSITION/PING/STATS, handles packets), asset thread |
| `App`, `Config` | Connection state, chat history, invitations; settings in `mmo_*.txt` |
| `NetworkClient`, `Players`, `NetSmooth.hpp` | Socket + handshake; remote players, local ranking book; jitter buffer and prediction |
| `Protocol.hpp`, `ServerConfig.example.hpp` | 64-byte packet, constants, default server (see [PROTOCOL.md](PROTOCOL.md)) |
| `GameState`, `GameStats` | Reads position, zone, camera, events, menus; ranking stats (badges, Pokédex, team...) |
| `GameHook` | The only per-frame hook: creates/moves/removes friends' native models, `!` emote |
| `GameMaps`, `TownMap`, `MapLowerPanes.hpp`, `HoennZones.hpp` | Pins on the game's AreaNav and Fly map; zone names |
| `SkinTex`, `SkinColors.hpp` | Outfit/hat colors written into the player's own texture |
| `HeroAssets`, `RomFS` | Brendan/May models and bikes read from the game's RomFS (no game files are shipped) |
| `Render3D`, `Math3D.hpp`, `FriendMotion.hpp` | Software 3D renderer for friends on bikes / with colors, character preview |
| `Follower3D`, `Lzma` | 3D followers from `mmo_pokes_3d.bin` (LZMA blocks in a fixed 640 KB arena) |
| `DepthProbe` | Copies the scene depth so buildings hide what is behind them |
| `Overlay` | Per-frame drawing: friends, followers (free-ground placement), names, chat, HUD, maps |
| `Ui`, `UiGfx` | SELECT menu (glass style, dirty-rectangle repaint, ~50 loops/s) |
| `Lang`, `LangTable.inc`, `GameText`, `GameFont`, `GameLang` | 7 languages: MMO texts, official names from the game, game font for Japanese/Korean, game language |
| `Perf.hpp`, `RankScore.hpp`, `Emotes`, `Species`, `Log` | FPS saver, ranking formula, emotes, species names, `mmo_log.txt` |
| `SafePatch.hpp` | Helper for code patches with JIT invalidation (currently unused) |

## 3. Rules that keep it stable

- **C++ gnu++11, no exceptions, no RTTI** (`-Os -fno-rtti -fno-exceptions`). Plugin heap ≈ 8 MB (`MemorySize: 10MiB`).
- **No large allocations per frame.** Reserve at boot (Render3D caches, follower arena, fonts, texts): heap
  fragmentation once made friends invisible.
- **Never block the game thread**: `send()`, DNS and file I/O go to the network/asset threads. The plugin only accepts
  IPv4 addresses for that reason.
- **Read the game's memory only through validated pointer chains** (`GameState.cpp`): a bad pointer can freeze the
  emulator. Writes to the game are limited to SkinTex, GameHook, hiding the head icon and GameLang; DepthProbe only
  reads. Anything new that writes must be justified and checked against the game build (`GameHook::Install` verifies it).
- Everything received from the network or read from files is bounds-checked (`Proto::CopyText`, slot ids 1..12,
  pack/RLE/LZMA sizes). Keep it that way.
- Performance budget: ~30 FPS with 4 drawn friends and 5 large followers in a Pokémon Center.

## 4. Build, test, debug

- Build: `make` in `3ds_plugin/` (requirements in the [README](../README.md#build-from-source)).
- Translations: `3ds_plugin/tools/i18n/strings.py` → `python 3ds_plugin/tools/i18n/gen_lang.py`.
- Server tests: `python -m unittest server/test_server_ranking.py server/test_server_limits.py`.
- Followers pack: [tools/pokepack](../tools/pokepack/README.md).
- **Fake players**: run `server/server.py` locally, point the plugin to `127.0.0.1` and start bots with
  `python tools/fake_players.py --zone 259 --cx 351 --cy 380 --h 2 --n 4 --variant 3 --lead 373,473,297,6`
  (options: `--bike`, `--emote N`, `--chat`, `--battle`, `--jitter ms`, `--listen --verbose` to decode what the plugin
  sends). Your zone and position are in `mmo_log.txt` (`diag:` lines).
- **Log**: `mmo_log.txt` next to the plugin (`perf:` FPS, `envio:` flags sent, `acomp:` followers, `menu:` menu timings,
  `profundidad:` depth captures).
- **Scripted tests** (developer only — delete the files afterwards):
  - `mmo_dev_walk.txt`: on boot the plugin presses A on the title screen, waits until you are in-game and runs
    `ms KEY [duration]` lines (`A B X Y L R START SELECT UP DOWN LEFT RIGHT`), `FOTO` (top screen → `mmo_dev_top_N.bmp`
    + depth → `mmo_dev_depth_N.bmp`), `FOTOB` (bottom screen) and `FIN`. **It never saves the game**; don't script
    A inside the X menu (the cursor may be on Save).
  - `mmo_dev_menu.txt`: opens the SELECT menu and walks every section (`demo`) or switches language 36 times
    (`idiomas`), taking screenshots.

## 5. Pull requests

See [CONTRIBUTING.md](../CONTRIBUTING.md).
