# 3D followers pack generator (`mmo_pokes_3d.bin`)

[Español](README_ES.md)

The 3D Pokémon followers of ORAS MMO are drawn from `mmo_pokes_3d.bin`. That file is made from the game's own 3D
models, so it is **not** shared here. This tool builds it on your PC from **your own copy** of Pokémon Omega Ruby or
Alpha Sapphire: all 721 Pokémon, 5 directions, a resting frame and 8 walking (or idle) frames each.

## What you need

- **Your own legal dump** of Pokémon Omega Ruby or Alpha Sapphire, in one of these forms:
  - a **decrypted** `.3ds` / `.cci` or `.cxi` file (the kind Azahar runs), or
  - a folder with the game's **extracted RomFS** (the folder that contains `a/0/0/8`).

  Encrypted files are rejected with a message. If you only have a `.cia`, extract its RomFS first with a tool such as
  GodMode9 (on your console), ctrtool or 3dstool.
- **Python 3.10 or newer** ([python.org](https://www.python.org/downloads/)).
- A graphics card with **OpenGL 3.3** (practically any PC from the last 10 years, with its driver up to date).

## Steps

Open a terminal in this folder (`tools/pokepack`) and run:

```
python -m pip install -r requirements.txt
python make_3d_pack.py --rom "C:\path\to\your_game.3ds"
```

It takes about 10 minutes with 4 processes (longer on slower PCs) and writes `mmo_pokes_3d.bin` (about 31 MB) in
the current folder.

Quick test first (Bulbasaur, Pikachu and Salamence, a few seconds), with a preview picture:

```
python make_3d_pack.py --rom "C:\path\to\your_game.3ds" --only 1,25,373 -o mmo_pokes_test.bin --sheet preview.png
```

Then copy `mmo_pokes_3d.bin` next to the plugin, in the same folder as `OrasMmo.3gx`:
`<Azahar user folder>/sdmc/luma/plugins/000400000011C400/` (Omega Ruby) and/or `.../000400000011C500/`
(Alpha Sapphire). Restart the game: your lead Pokémon now follows you.

## Options

| Option | What it does |
|---|---|
| `--rom PATH` | Decrypted `.3ds` / `.cci` / `.cxi`, or the extracted RomFS folder (required) |
| `-o FILE` | Output file (default `mmo_pokes_3d.bin`) |
| `--workers N` | Processes in parallel (default 4; use 1 if your PC runs out of memory) |
| `--only 1,25,373` | Only these National Dex numbers (for tests) |
| `--sheet FILE.png` | Also saves a picture with the walk frames, to check the result |

## Good to know

- The `.bin` contains game graphics: **keep it for yourself**, do not upload or share it. The repository's
  `.gitignore` and `tools/prepublish_check.py` already block `mmo_pokes_*.bin`.
- A different graphics card or library version may change a few pixels. That is harmless: the plugin reads the file
  the same way.
- Files: `make_3d_pack.py` (main script and file format), `bake.py` (camera, size, pose), `walk.py` (walk cycle),
  `pkrender.py` (OpenGL renderer), `pkmodel.py` / `pkanim.py` (Pokémon models and animations), `bch.py` / `pica.py`
  (3DS model format), `gfmotion.py` (Game Freak motions), `romfs.py` / `lgarc.py` (reading the game files).
