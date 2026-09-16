# Project ORAS MMO - Safety and Encounter QA Report

## Pass 1: Correctness & Platform
- Verified C++ syntax in `main.cpp` and `Entity.hpp`.
- Confirmed `Process::CheckAddress` is properly guarding pointer dereferences.
- Added `clear_player(int slot)` in `Entity.hpp` to securely hide AND zero out the `modelID` of hijacked NPC slots, resolving memory instability.
- Verified bounds in `find_entity_base()` remain strictly within `0x08C00000 - 0x08E00000`.

## Pass 2: Save Game Safety
- Implemented `g_isSaving` atomic flag hook. When saving starts (monitored via `g_saveStateAddr`), all entity writes are immediately paused.
- Before pausing, all active NPC slots are scrubbed (visibility = 0, modelID = 0) to prevent corrupting the local save file.
- Safe shutdown cleanup added when the plugin thread closes.
- 20-minute CTRPF OSD auto-reminder logic added.

## Pass 3: Encounter Independence
- Added explicit design documentation regarding encounter tables.
- Confirmed that the server ONLY relays Position packets `(X, Y, MapID, Facing)` and no battle states or RAM seeds are shared.
- Map filtering ensures players outside the current map do not exist locally.
- Fixed the `OnFrameCallback` to robustly detect when the local player's entity disappears into a battle, properly triggering the `PKT_BATTLE_START` event.
