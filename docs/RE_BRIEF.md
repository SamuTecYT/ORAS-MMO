# Project ORAS MMO — Reverse Engineering Technical Brief

> **Date:** 2026-09-14  
> **Author:** RE Agent (automated research)  
> **Status:** Initial research pass — offsets pending live validation  
> **Scope:** Pokémon Omega Ruby / Alpha Sapphire (3DS) — all regions

---

## Table of Contents

1. [Game Identity & Title IDs](#1-game-identity--title-ids)
2. [3DS Memory Architecture Overview](#2-3ds-memory-architecture-overview)
3. [ORAS RAM Offsets — Current State of Research](#3-oras-ram-offsets--current-state-of-research)
4. [ExeFS / code.bin — Extraction, Patching & Repacking](#4-exefs--codebin--extraction-patching--repacking)
5. [ARM11 Code Injection — Hook Feasibility Assessment](#5-arm11-code-injection--hook-feasibility-assessment)
6. [Citra Mobile (Android) — SOC:U Socket Emulation](#6-citra-mobile-android--socu-socket-emulation)
7. [Community Research — Existing Projects & Disassembly](#7-community-research--existing-projects--disassembly)
8. [Tool List with Download Links](#8-tool-list-with-download-links)
9. [Recommended Next Steps](#9-recommended-next-steps)
10. [Caveats & Blockers](#10-caveats--blockers)

---

## 1. Game Identity & Title IDs

| Game | Title ID (all regions) | Product Code |
|---|---|---|
| Pokémon Omega Ruby | `000400000011C400` | ECRA |
| Pokémon Alpha Sapphire | `000400000011C500` | ECLA |
| ORAS Special Demo | `000400000014A300` | — |

> NOTE: The title ID `0004000000177000` referenced in the initial brief template is INCORRECT. The confirmed correct IDs are above, verified against the 3DS title database.

ORAS uses a **single Title ID shared across NA, EUR, and JPN** regions. The regional variant is encoded only in the Product Code suffix. This simplifies LayeredFS patching — one patch folder covers all regions of the same game.

**Version note:** ORAS shipped in v1.0 and received updates up to v1.4. Cheat codes and pointer offsets are **version-specific**. Always target the base game ID (`00040000...`) for code.bin patches via Luma3DS.

---

## 2. 3DS Memory Architecture Overview

### CPU
- **ARM11 Cortex-A9 MPCore** — dual-core (O3DS/2DS) or quad-core (N3DS/N2DS)
- 32-bit ARMv6K ISA with VFPv2 float support
- Little-endian byte order

### Physical RAM
| Platform | FCRAM Physical Base | Size |
|---|---|---|
| O3DS / 2DS | `0x20000000` | 128 MB |
| N3DS / N2DS XL | `0x20000000` | 256 MB (extra block at `0x28000000`) |

### Virtual Address Space (per process)
The 3DS MMU gives each process its own virtual address space. Key regions:

| Region | Virtual Range | Description |
|---|---|---|
| Code (`.text`) | `0x00100000` — `~0x00EFFFFF` | Game executable — **code.bin loads here** |
| `.rodata` | Follows `.text` | Read-only data |
| `.data` | Follows `.rodata` | Initialized mutable data |
| Heap | `0x08000000`+ | Dynamic allocations (game entity objects) |
| Stack | Near `0x10000000` | Per-thread stack |
| Shared | `0x10000000`+ | System shared memory (IPC buffers) |
| FCRAM (mapped) | `0x20000000`+ | Physical RAM |

**KEY INSIGHT:** There is **no ASLR** on 3DS — `code.bin` virtual base is deterministic at `0x00100000`. However, **heap allocations are dynamic**. Player position, party data, and map ID are stored in heap-allocated structs, so their absolute virtual addresses change each session. You must use **pointer chains** or **function hooks** to locate them reliably.

---

## 3. ORAS RAM Offsets — Current State of Research

### Summary Finding

**No publicly maintained, static RAM map for ORAS player coordinates or live game state exists.** Unlike GBA/DS Pokémon games with fixed WRAM offsets, the 3DS uses a fully dynamic heap for game objects. The community works around this via:

1. **NTR / CTRPluginFramework plugins** — hook game functions at runtime, compute addresses via pointer chains
2. **Luma3DS Rosalina / Cheat Engine + Citra** — live memory scanning per-session

### Field Status Table

| Field | Type | Status | Notes |
|---|---|---|---|
| `player_x` | float32 | ❌ Unknown | In overworld entity struct on heap. Scan via Citra+CE float search while moving E/W |
| `player_y` | float32 | ❌ Unknown | Same struct as X. Move N/S to isolate |
| `player_z` | float32 | ❌ Unknown | 3D height coordinate; present but rarely varies in flat overworld |
| `map_id` | uint16 | ❌ Unknown | Zone/map ID. PKHeX SAV6 documents save-file offsets; in-RAM pointer needs runtime discovery |
| `facing_direction` | uint8 | ❌ Unknown | Expected: 0=Down, 1=Up, 2=Left, 3=Right (verify in player struct) |
| `battle_state` | uint8/enum | ❌ Unknown | Scan for 0→nonzero transition when entering wild encounter |
| `cutscene_flag` | uint8 | ❌ Unknown | Non-zero when event script locks player input |
| `party_pointer` | ptr32 | ❌ Unknown | Points to PK6 (decrypted) Pokémon array; each entry is 0xE8 bytes (Gen 6 struct) |
| `money` | uint32 | ❌ Unknown | Easiest anchor — search exact current Pokédollar amount as 4-byte int |

### Why Static Offsets Don't Exist

ORAS allocates its game-world manager, player entity, and party data structures on the **heap at runtime**. Addresses are not fixed across sessions. The correct approach is:

1. Find a **static/near-static pointer** in the `.data` section that references the heap object
2. **Follow the pointer chain** at runtime to reach coordinate/map fields
3. Hook the code that **writes** to those fields (discovered via GDB watchpoints)

### Offset Discovery Methodology

```
STEP 1: Set up Citra/Azahar desktop with ORAS loaded
STEP 2: Attach Cheat Engine to Citra/Azahar process

For player_x:
  a. Search type: float32, unknown initial value
  b. Move player East → filter "increased"
  c. Stand still → filter "unchanged"
  d. Move West → filter "decreased"
  e. Repeat until ~1–5 candidates remain

For money:
  a. Note exact Pokédollar amount
  b. Search type: 4-byte, exact value
  c. Spend/earn money → search for new value
  d. Typically converges in 2–3 rounds

For map_id:
  a. Search type: 2-byte, unknown initial value
  b. Enter a building/zone → filter "changed"
  c. Stand still → filter "unchanged"
  d. Cross-reference value against PKHeX zone ID list

STEP 3: For each discovered heap address, use GDB watchpoint:
  watch *(uint32_t*)0x<address>
  This shows what instruction WRITES to it → reveals the player struct pointer

STEP 4: Trace back the pointer chain to find the .data-section anchor pointer
STEP 5: Document both the anchor address and the struct field offsets
```

### PKHeX as Save-Format Reference

PKHeX open-sources the Gen 6 save format. While these are save-file offsets (not RAM), the field layout in RAM is structurally identical:

- `SAV6.cs` — documents all save block offsets including `Money`, `MapID`, party pointers
- `PK6.cs` — documents individual Pokémon data structure (0xE8 bytes each)
- Available at: `github.com/kwsch/PKHeX`

---

## 4. ExeFS / code.bin — Extraction, Patching & Repacking

### ExeFS Structure

```
ExeFS/
  code.bin       — Main ARM11 executable (LZ77 compressed in ORAS)
  banner.bin     — Game banner (icon + sound)
  icon.bin       — Home menu icon
```

`code.bin` loads at virtual address `0x00100000`. Size: ~5–8 MB uncompressed for ORAS.

### Step 1: Get a Decrypted ROM

Use **GodMode9** on a CFW 3DS to dump a decrypted `.cia` or `.3ds` file. Third-party decrypted ROMs are not sanctioned here — always dump from your own cartridge.

### Step 2: Extract ExeFS

```bash
# Using ctrtool — extract NCCH content first
ctrtool --intype=cia --content=content.cxi input.cia

# Then extract ExeFS
ctrtool -x --intype=cxi --exefsdir=exefs/ --exefsheader=exefsheader.bin content.0000.cxi

# --- OR using 3dstool ---
# First extract exefs.bin from the NCCH
3dstool -xvtf cxi content.cxi --exefs exefs.bin --exefs-header exefsheader.bin

# Then extract files from exefs.bin
3dstool -xvtf exefs exefs.bin --exefs-dir exefs/ --header exefsheader.bin
```

### Step 3: Decompress code.bin

ORAS's code.bin is **BLZ/LZ11-compressed**. You MUST decompress before patching:

```bash
# blz tool (part of devkitARM toolchain or standalone)
blz -d code.bin
# Produces decompressed code.bin in-place or as code_dec.bin

# Verify: decompressed file should start with ARM instructions, not LZ magic bytes
xxd code.bin | head -4
```

> **CAUTION:** Patching the compressed binary directly will corrupt the file. Always decompress first.

### Step 4: Apply IPS Patch

**Method A: Luma3DS LayeredFS (RECOMMENDED)**

No ROM repacking required. Place `code.ips` on your SD card:

```
SD:/luma/titles/000400000011C400/code.ips   ← Omega Ruby
SD:/luma/titles/000400000011C500/code.ips   ← Alpha Sapphire
```

Enable in Luma3DS: Hold SELECT on boot → Toggle "Enable game patching" → Press START.

Luma3DS patches the decompressed code at load time — it handles decompression internally.

**Method B: Direct file edit**

```bash
# Apply IPS with any standard IPS patcher (e.g., Lunar IPS on Windows)
# Then recompress:
blz -en code.bin

# Repack ExeFS:
3dstool -cvtf exefs exefs.bin --exefs-dir exefs/ --header exefsheader.bin

# Repack full CIA with makerom (advanced — requires ExHeader)
makerom -f cia -o output.cia -content content.cxi:0:0 ...
```

### IPS Offset Note

IPS offsets are **file offsets from the start of code.bin**, NOT virtual addresses.

```
file_offset = virtual_address - 0x00100000

Example:
  Virtual address: 0x00201234
  File offset:     0x00101234
```

---

## 5. ARM11 Code Injection — Hook Feasibility Assessment

### Verdict: FEASIBLE — Moderate Difficulty

### A. Code Cave Discovery

ORAS code.bin contains null-padded regions between sections. These are code cave candidates.

**Method:**
1. Load `code.bin` in Ghidra at base `0x00100000`, language: ARM v6 little-endian
2. Use Search → Memory → `\x00\x00\x00\x00` with min length 64+ bytes in the `.text` section
3. Verify region is in the executable (`.text`) bounds, not `.data`
4. Caves of ≥ 256 bytes are sufficient for a basic sync hook

### B. Hook Trampoline Pattern (ARM32)

```arm
; ======================================
; At hook site (4 bytes, replaces one ARM instruction):
; ======================================
    BL   cave_addr          ; Branch to code cave (4 bytes)

; ======================================
; In code cave:
; ======================================
cave_addr:
    PUSH {r0-r3, lr}        ; Save registers                  [4 bytes]

    ; -- Read player struct pointer --
    LDR  r0, =player_mgr_static_ptr    ; Load addr of static ptr  [4 bytes]
    LDR  r0, [r0]                       ; Dereference → struct base [4 bytes]
    
    ; -- Read coordinates --
    VLDR s0, [r0, #0x10]    ; Player X (float, offset TBD)    [4 bytes]
    VLDR s1, [r0, #0x14]    ; Player Y (float, offset TBD)    [4 bytes]
    
    ; -- Read map_id --
    LDRH r1, [r0, #0x??]    ; Map ID (uint16, offset TBD)     [4 bytes]
    
    ; -- Write to shared sync buffer (fixed address in .data) --
    LDR  r2, =sync_buffer_addr          ; Fixed buffer address    [4 bytes]
    VSTR s0, [r2, #0x00]    ; Write X                          [4 bytes]
    VSTR s1, [r2, #0x04]    ; Write Y                          [4 bytes]
    STRH r1, [r2, #0x08]    ; Write map_id                     [4 bytes]

    POP  {r0-r3, lr}        ; Restore registers                [4 bytes]
    
    ; -- Trampoline: execute replaced instruction --
    <original 4 bytes from hook site>   ; Replaced instruction
    BX   lr                 ; Return to hook site + 4          [4 bytes]
```

**Total cave size needed: ~64–128 bytes** (well within any reasonable cave)

### C. BL Offset Calculation

```
ARM32 BL encoding:
  Bits[31:28] = 0xE (condition = always)
  Bits[27:24] = 0xB (BL opcode)
  Bits[23:0]  = signed offset = (target - source - 8) / 4

Example:
  Source: 0x00200000 (hook site in code.bin)
  Target: 0x00210000 (code cave)
  Offset = (0x00210000 - 0x00200000 - 8) / 4 = 0x3FFE
  Opcode = 0xEB003FFE → little-endian bytes: FE 3F 00 EB
```

### D. Best Hook Point Candidates

| Target | Rationale | Discovery Method |
|---|---|---|
| Overworld update tick | Called every frame; player position is current | Set GDB breakpoint while walking, observe frequent hits |
| Map transition handler | Fires on zone change; captures new map_id | Breakpoint triggered when entering a building |
| Battle entry function | Fires when wild/trainer battle starts | Set breakpoint, enter tall grass |
| Battle exit function | Fires on battle end | Follow from battle entry |

### E. Networking Architecture (CRITICAL)

> **You CANNOT make raw TCP socket calls from code.bin injected code on real 3DS hardware.**

Socket calls require the SOC:U service via IPC (Inter-Process Communication), which involves supervisor calls (SVCs) that cannot be trivially invoked from a raw ARM hook without a full service wrapper.

**Recommended architecture:**

```
[Injected hook in code.bin]
        |
        | writes player state to
        v
[Fixed shared memory buffer at known .data address]
        |
        | read by
        v
[NTR/CTRPluginFramework .3gx plugin (separate process)]
        |
        | calls SOC:U via IPC
        v
[TCP socket to MMO server]
```

**On Azahar/Citra (emulator):** Use the GDB stub instead of code injection for early prototyping:

```
[Python bridge script]
  connects to Azahar GDB stub (localhost:24689)
  reads player_x, player_y, map_id from known addresses
  forwards to MMO server via TCP
```

This requires **zero ROM modification** and validates the concept before tackling code.bin surgery.

### F. Thumb vs ARM Mode Note

ORAS uses mixed ARM32 and Thumb-2 instructions. Check your hook site:
- Odd address → Thumb mode (use `BLX`)
- Even address → ARM32 mode (use `BL`)
- Ghidra decodes mode automatically

---

## 6. Citra Mobile (Android) — SOC:U Socket Emulation

### Emulator Status Matrix (as of September 2026)

| Emulator | Platform | SOC:U HLE | TCP External | Recommended |
|---|---|---|---|---|
| **Azahar** | Desktop + Android | ✅ Full | ✅ Yes | ✅ YES |
| **Lime3DS** | Desktop + Android | ✅ Full | ✅ Yes | ✅ Yes |
| **Citra MMJ** | Android only | ⚠️ LAN only | ❌ No | ❌ Outdated |
| **Citra** (original) | Desktop only | ✅ Full | ✅ Yes | ⚠️ Archived |

### SOC:U HLE Implementation

All Citra-derived emulators implement SOC:U as a **High-Level Emulation service**:

```
Game code → soc:Connect IPC call
              ↓
          [Emulator intercepts via HLE dispatcher]
              ↓
          soc_u.cpp service handler
          translates to POSIX socket() / connect() / send() / recv()
              ↓
          Host OS network stack (Android NDK libc on mobile)
              ↓
          Real TCP/IP connection
```

Source code location: `src/core/hle/service/soc/soc_u.cpp` in Azahar/Lime3DS repos.

### Android-Specific Behavior

- Uses Android NDK `libc` socket functions — functionally POSIX-identical
- Required manifest permissions: `INTERNET` + `ACCESS_NETWORK_STATE` (declared in official Azahar build)
- TCP to external IP addresses: **CONFIRMED WORKING** (Azahar's built-in multiplayer room system proves this)
- Official Nintendo Network: ❌ (no auth credentials)
- Pretendo Network: ✅ Partially (closed beta for ORAS, requires DNS redirect + subscription)

### Conclusion for MMO Architecture

**A game running in Azahar (Android or desktop) CAN open a TCP socket to an external MMO server**, provided:
1. The game's code (or injected code + plugin) calls the SOC:U IPC interface correctly
2. The server is reachable from the device's network
3. Required Android permissions are present (they are in Azahar)

**For emulator-only prototype:** The GDB stub + external Python bridge is the fastest path (no ROM modification).

**For production (real hardware + emulator):** A CTRPluginFramework `.3gx` plugin reads the shared buffer written by the code.bin hook and handles TCP via SOC:U IPC.

---

## 7. Community Research — Existing Projects & Disassembly

### Existing ORAS Multiplayer Projects

| Project | Description | Status |
|---|---|---|
| **Pretendo Network** | Nintendo Network replacement for 3DS/Wii U | ORAS in closed beta (mid-2026); not fully public yet |
| **Azahar Multiplayer Rooms** | Emulator LAN emulation for existing wireless features | Functional; limited to original game's network protocol |
| **Custom ORAS MMO** | True persistent-world MMO server hack | **Does not exist — this project would be the first** |

### Disassembly / Decompilation Status

| Project | Status |
|---|---|
| **pret organization** (pokeemerald, pokecrystal, etc.) | Gen 6 NOT started — only covers Gen 1–5 |
| **Partial ORAS RE** | Scattered GBATemp posts, no central repository |
| **PKHeX** | Authoritative save-file format documentation (Gen 6); most useful reference |
| **Gen6CTRPluginFramework** | Closest to ORAS-specific runtime RE; shows memory access patterns |

### Key Community Repositories for ORAS Hacking

```
Gen6 CTRPlugin Framework (biometrix76):
  github.com/biometrix76/Gen6CTRPluginFramework

Gen6 Plugin Overhauled (samaBR85):
  github.com/samaBR85/Gen6CTRPFrameworkOverhauled

PokemonCheatPlugin (Hartie95):
  github.com/Hartie95/PokemonCheatPlugin

ORAS Chinese 1.4 NTR Cheat (easyworld):
  github.com/easyworld/Pokemon-Omega-Ruby-CHS-1.4-NTR-Cheat

Sharkive cheat DB (FlagBrew):
  github.com/FlagBrew/Sharkive

PKHeX save editor (kwsch):
  github.com/kwsch/PKHeX
  → SAV6.cs for Gen 6 save field offsets
  → PK6.cs for Pokémon struct layout
```

### Gen 6 RAM Map Documentation Status

No comprehensive public RAM map exists for ORAS. Best available resources:

1. **PKHeX `SAV6.cs`** — authoritative save-file field offsets (translatable to RAM layout)
2. **3dbrew wiki** (3dbrew.org) — 3DS system internals (memory map, IPC services) — not game-specific
3. **GBATemp "ORAS Cheats" threads** — community Gateshark/NTR codes embed specific version addresses
4. **CTRPluginFramework repos above** — study source for runtime pointer chain patterns

---

## 8. Tool List with Download Links

### ROM Analysis & Extraction

| Tool | Purpose | Link |
|---|---|---|
| **ctrtool** | Extract NCCH, ExeFS, RomFS from 3DS ROMs | https://github.com/3DSGuy/Project_CTR |
| **3dstool** | Extract and repack ExeFS, RomFS archives | https://github.com/dnasdw/3dstool |
| **makerom** | Repack NCCH/CIA containers | https://github.com/3DSGuy/Project_CTR (same repo) |
| **GodMode9** | Dump decrypted ROMs from real 3DS hardware | https://github.com/d0k3/GodMode9 |
| **blz** | BLZ compress/decompress code.bin | Bundled with devkitARM; search GBATemp for standalone |

### Patching Tools

| Tool | Purpose | Link |
|---|---|---|
| **Lunar IPS** | Apply/create IPS patches (Windows) | https://www.romhacking.net/utilities/240/ |
| **Luma3DS** | CFW with LayeredFS + code.ips support | https://github.com/LumaTeam/Luma3DS |

### Disassembly / Reverse Engineering

| Tool | Purpose | Link |
|---|---|---|
| **Ghidra** | Primary free disassembler; load code.bin at 0x00100000 | https://ghidra-sre.org |
| **3ds-Ghidra-Scripts-Java** | Ghidra scripts for 3DS code.bin | https://github.com/zaksabeast/3ds-Ghidra-Scripts-Java |
| **IDA Pro** (commercial) | Professional disassembler | https://hex-rays.com |
| **3ds_ida** | IDA loader for 3DS code.bin | https://github.com/kynex7510/3ds_ida |
| **devkitARM** | ARM11 cross-compiler (arm-none-eabi-gcc) | https://devkitpro.org |

### Emulation & Debugging

| Tool | Purpose | Link |
|---|---|---|
| **Azahar** | Primary 3DS emulator (Citra successor); GDB stub + SOC:U | https://github.com/azahar-emu/azahar |
| **Lime3DS** | Active Citra fork, Android + desktop | https://github.com/Lime3DS/Lime3DS |
| **Citra** (archived) | Reference implementation | https://github.com/citra-emu/citra |
| **Cheat Engine** | Memory scanner; attach to Citra/Azahar process | https://www.cheatengine.org |
| **arm-none-eabi-gdb** | GDB for ARM; connect to emulator GDB stub | Bundled with devkitARM |

### Save File & Plugin Development

| Tool | Purpose | Link |
|---|---|---|
| **PKHeX** | Gen 6 save editor; best field layout reference | https://projectpokemon.org/home/files/file/1-pkhex/ |
| **Checkpoint** | 3DS save backup/restore homebrew | https://github.com/FlagBrew/Checkpoint |
| **CTRPluginFramework** | Framework for 3DS `.3gx` runtime plugins | https://github.com/Nanquitas/CTRPluginFramework |

---

## 9. Recommended Next Steps

### Phase 1: Offset Discovery (Est. 1–2 weeks)

```
[ ] Install Azahar desktop
[ ] Load ORAS, enable GDB stub (Emulation → Configure → Debug → GDB Stub port 24689)
[ ] Attach Cheat Engine to Azahar process
[ ] Scan for: money (4-byte exact), player_x/y (float32), map_id (2-byte)
[ ] Use GDB watchpoint on each discovered address → identify writer function
[ ] Trace pointer chain from writer back to .data-section static pointer
[ ] Update offsets/offsets.json with all discovered values
```

### Phase 2: code.bin Static Analysis (Est. 2–4 weeks)

```
[ ] Dump decrypted ORAS code.bin via Azahar or GodMode9
[ ] Run: blz -d code.bin
[ ] Import into Ghidra: Format=Raw Binary, Base=0x00100000, Lang=ARM v6 LE
[ ] Apply 3ds-Ghidra-Scripts for SVC labels and segment splitting
[ ] Cross-reference GDB function addresses to Ghidra to label overworld tick
[ ] Find code cave candidates (null-padded regions in .text, ≥ 256 bytes)
[ ] Draft hook IPS patch (trampoline + cave code)
```

### Phase 3: Prototype (Est. 2–4 weeks)

```
[ ] Write Python script connecting to Azahar GDB stub, reading XYZ + map_id
[ ] Forward position data to a local test server via TCP
[ ] Validate data matches in-game position display
[ ] Design binary protocol for MMO server (position update packet structure)
[ ] Stand up Go/Node.js MMO server stub that echoes positions of all connected clients
```

### Phase 4: code.bin Hook (Est. 4–8 weeks)

```
[ ] Write ARM32 hook assembly (position reader + shared buffer writer)
[ ] Assemble with arm-none-eabi-as
[ ] Generate IPS patch (calculate BL offset, encode cave bytes)
[ ] Test via Luma3DS LayeredFS code.ips in Azahar
[ ] Verify no ARM11 exception; check /luma/dumps/arm11/ if crash
[ ] Connect to CTRPluginFramework .3gx plugin for TCP transmission
```

### Phase 5: Android Deployment

```
[ ] Test complete pipeline on Azahar Android
[ ] Confirm SOC:U TCP connection to MMO server works on mobile
[ ] Package Luma3DS plugin + code.ips as user-distributable archive
[ ] Document setup instructions for end users
```

---

## 10. Caveats & Blockers

### Critical Issues

| Issue | Severity | Mitigation |
|---|---|---|
| No static RAM offsets — all player data is heap-dynamic | 🔴 High | Pointer chain discovery via Cheat Engine + GDB (Phase 1) |
| No ORAS disassembly project — starting from scratch | 🔴 High | Ghidra + community scripts + PKHeX reference |
| code.bin is LZ77-compressed — decompress before any analysis | 🟡 Medium | `blz -d code.bin` — straightforward but easy to overlook |
| Direct SOC:U IPC from injected hook is very complex | 🔴 High | Use CTRPlugin .3gx as network layer (separate process) |
| ORAS v1.0 vs v1.4 — offsets differ between versions | 🟡 Medium | Target v1.4 consistently for all development |
| Pretendo Network doesn't cover ORAS publicly yet | 🟢 Low | Build custom MMO server (doesn't need Pretendo at all) |
| Real hardware TCP requires SOC:U IPC | 🔴 High | Architecture around CTRPlugin plugin for networking |
| Citra MMJ (Android) is outdated/limited | 🟢 Low | Use Azahar instead — active development, proper SOC:U |

### Hard Blockers (Worst Case)

1. **No executable code cave in .text:** If ORAS's compiled .text is packed with no null padding ≥ 64 bytes, the trampoline target has no home.
   - **Mitigation:** Append a new section via custom ExHeader + makerom (advanced), or increase .text size. Luma3DS can also load an external code segment file.

2. **ExeFS hash verification failure:** Modified code.bin may fail NCCH hash checks.
   - **Mitigation:** Luma3DS LayeredFS bypasses this entirely (patches in memory, never writes to ROM). For standalone ROM distribution, use makerom to recalculate hashes.

3. **ARM11 exception on bad hook:** Miscalculated BL offset or corrupted replaced instruction causes immediate crash.
   - **Mitigation:** Check `/luma/dumps/arm11/` for crash address. Verify instruction mode (ARM vs Thumb) at hook site. Use Azahar's debugger before testing on hardware.

4. **Azahar GDB stub timing:** Memory reads via GDB may lag 1–3 frames behind actual game state.
   - **Mitigation:** For prototype, this is acceptable. For production, hook-based approach gives frame-accurate data.

---

## Appendix A: 3DS Virtual Memory Map (Simplified)

```
ARM11 User Process Virtual Space:
┌──────────────────────────────────────────────────────┐
│ 0x00000000 – 0x000FFFFF  │ Unmapped / null guard     │
│ 0x00100000 – 0x00EFFFFF  │ code.bin (.text+data)     │  ← code loads here
│ 0x00F00000 – 0x07FFFFFF  │ (unmapped / reserved)     │
│ 0x08000000 – 0x0FFFFFFF  │ Heap (player struct, etc) │  ← dynamic allocs
│ 0x10000000 – 0x1FFFFFFF  │ System / IPC / shared mem │
│ 0x20000000 – 0x27FFFFFF  │ FCRAM physical mapping    │
│ 0x28000000 – 0x2FFFFFFF  │ FCRAM extra (N3DS only)   │
└──────────────────────────────────────────────────────┘
```

## Appendix B: IPS Patch Format

```
Header:  50 41 54 43 48  ("PATCH") — 5 bytes
Records: [offset: 3 bytes BE] [size: 2 bytes BE] [data: size bytes]
Footer:  45 4F 46  ("EOF") — 3 bytes

IMPORTANT:
  Luma3DS code.ips offsets are FILE offsets from start of code.bin.
  Conversion: file_offset = virtual_address - 0x00100000
```

## Appendix C: ARM32 BL Encoding Reference

```
BL <target>  (branch with link, ARM32, little-endian)

Opcode:   0xEB______
Offset:   (target_vaddr - source_vaddr - 8) / 4  [signed 24-bit]

Example:
  Source:  0x00201000
  Target:  0x00208000 (code cave)
  Offset:  (0x00208000 - 0x00201000 - 8) / 4 = 0x1BFE
  Word:    0xEB001BFE
  Bytes:   FE 1B 00 EB  (little-endian, write to code.bin at file offset 0x00101000)
```

## Appendix D: Gen 6 Pokémon Struct (PK6) — Key Fields

```
Offset  Size  Field
0x00    4     Encryption Constant
0x04    2     Sanity (always 0)
0x06    2     Checksum
// Block A (0x08–0x27)
0x08    2     Species
0x0A    2     HeldItem
0x0C    4     TID/SID
0x10    4     EXP
// Block B (0x28–0x47)  — Moves
// Block C (0x48–0x67)  — EVs, Contest stats
// Block D (0x68–0x87)  — Nickname, Trainer name
// Party-only data (beyond 0x88):
0xE0    4     Current HP
0xE4    4     Max HP (stat)
... etc (party struct is 0xE8 bytes total)
```

---

*RE Brief v1.0 — Project ORAS MMO — Generated 2026-09-14*
*All offsets marked null require live memory analysis for confirmation.*
*For questions: see recommended tool list in §8 and methodology in §3.*
