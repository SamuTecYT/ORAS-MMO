<p align="center">
  <img src="../../docs/img/followers.png" alt="A trainer with a 3D Salamence following behind" width="560"/>
</p>

# 🐉 3D followers pack generator (`mmo_pokes_3d.bin`)

**English** · [Español](README_ES.md)

The 3D Pokémon followers of ORAS MMO are drawn from `mmo_pokes_3d.bin`. That file is made from the game's own 3D
models, so it is **not** shared here. This tool builds it on your PC from **your own copy** of Pokémon Omega Ruby or
Alpha Sapphire: all 721 Pokémon, 5 directions, a resting frame and 8 walking (or idle) frames each.

## ✅ What you need

- [ ] **Your own copy** of Pokémon Omega Ruby or Alpha Sapphire, as either:
  - a **decrypted** `.3ds` / `.cci` or `.cxi` file (the kind Azahar runs), or
  - a folder with the game's **extracted RomFS** (the folder that contains `a/0/0/8`).
- [ ] **Python 3.10 or newer** ([python.org](https://www.python.org/downloads/); on Windows tick ✅ *Add Python to PATH*).
- [ ] A graphics card with **OpenGL 3.3**: practically any PC from the last 10 years, with its driver up to date.

> [!NOTE]
> Encrypted files are rejected with a message. If you only have a `.cia`, extract its RomFS first with a tool such
> as GodMode9 (on your console), ctrtool or 3dstool.

## 🚀 Steps

1. Open a terminal in this folder (`tools/pokepack`).
2. Install what the tool needs (only the first time):
   ```bash
   python -m pip install -r requirements.txt
   ```
3. **Optional quick test** (Bulbasaur, Pikachu and Salamence, a few seconds), with a preview picture:
   ```bash
   python make_3d_pack.py --rom "C:\path\to\your_game.3ds" --only 1,25,373 -o mmo_pokes_test.bin --sheet preview.png
   ```
4. **Build the full pack.** It takes about **10 minutes** with 4 processes (longer on slower PCs):
   ```bash
   python make_3d_pack.py --rom "C:\path\to\your_game.3ds"
   ```
   You get `mmo_pokes_3d.bin` (about 31 MB) in the current folder.
5. **Copy it next to the plugin** and restart the game. Your lead Pokémon now follows you! 🎉

| Where you play | Copy `mmo_pokes_3d.bin` to |
|---|---|
| 🪟 PC, Omega Ruby | `<Azahar user folder>/sdmc/luma/plugins/000400000011C400/` |
| 🪟 PC, Alpha Sapphire | `<Azahar user folder>/sdmc/luma/plugins/000400000011C500/` |
| 🤖 Android | Azahar's `sdmc/luma/plugins/` (next to `default.3gx`) |

> [!TIP]
> On Windows you can also put it next to `INSTALL_PC.bat` and run the installer again: it copies it for both games.

## ⚙️ Options

| Option | What it does |
|---|---|
| `--rom PATH` | Decrypted `.3ds` / `.cci` / `.cxi`, or the extracted RomFS folder (required) |
| `-o FILE` | Output file (default `mmo_pokes_3d.bin`) |
| `--workers N` | Processes in parallel (default 4; use 1 if your PC runs out of memory) |
| `--only 1,25,373` | Only these National Dex numbers (for tests) |
| `--sheet FILE.png` | Also saves a picture with the walk frames, to check the result |

## 💡 Good to know

> [!WARNING]
> The `.bin` contains game graphics: **keep it for yourself**, don't upload or share it. The repository's
> `.gitignore` and `tools/prepublish_check.py` already block `mmo_pokes_*.bin`.

- A different graphics card or library version may change a few pixels. That's harmless: the plugin reads the file
  the same way.
- Files: `make_3d_pack.py` (main script and file format), `bake.py` (camera, size, pose), `walk.py` (walk cycle),
  `pkrender.py` (OpenGL renderer), `pkmodel.py` / `pkanim.py` (Pokémon models and animations), `bch.py` / `pica.py`
  (3DS model format), `gfmotion.py` (Game Freak motions), `romfs.py` / `lgarc.py` (reading the game files).
