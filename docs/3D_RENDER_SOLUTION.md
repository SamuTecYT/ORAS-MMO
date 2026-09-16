# 3D Render Solution for ORAS MMO

## 1. Finding the Overworld Entity Manager / NPC list pointer dynamically
Due to dynamic memory allocation and Address Space Layout Randomization (ASLR), a static pointer is not reliable across different sessions or randomized ROMs. The reliable way to find the pointer chain dynamically is:
1. **Pattern Scanning:** Scan the game's executable code (usually in the `code.bin` mapped in memory) for the specific assembly instruction sequence that references the Entity Manager's base pointer. This pointer is typically stored in the `.bss` or `.data` section of the main executable.
2. **Relative Offset Calculation:** Once the pattern is found, extract the relative offset from the assembly instruction and calculate the absolute memory address of the Entity Manager pointer.
3. **Pointer Dereferencing:** Dereference this base pointer to find the start of the dynamic Entity List (or Overworld State structure). 
4. **Alternative for GDB:** Since you have a PC client using GDB, you can automate this memory pattern scan across the `.text` region via the GDB stub to find the base pointer on startup.

## 2. Memory Layout of an Overworld Entity (Gen 6 ORAS)
According to community research (e.g., Project Pokémon), the overworld entity data structure is allocated in blocks of **0x108 bytes**.
The approximate structure (needs refinement based on specific entity type) is:
```cpp
struct OverworldEntity {
    // Note: Exact offsets require manual verification in debugger
    uint32_t entityID;         // Often at 0x00
    uint32_t modelID;          // Usually early in the struct
    float x;                   // Known offsets in relation to entity base
    float y;                   
    float z;                   
    uint32_t facingDirection;  
    uint32_t visibilityFlag;   
    // ... padding to 0x108 bytes
};
```
*Note: The player's X/Y/Z offsets you have (0x08C6E894, etc.) are inside one such structure in the FCRAM.*

## 3. Hijacking an Existing NPC Slot
To safely replace an NPC with a remote player:
1. **Identify an unused or background NPC:** Iterate through the Entity List array (size 0x108 per element). Find an entity that is not critical to the immediate map logic (e.g., a generic townsfolk).
2. **Modify Model ID:** Overwrite the `modelID` field with the Brendan or May model ID.
3. **Update Coordinates:** Continuously write the remote player's X, Y, Z coordinates to the struct via the GDB stub.
4. **Update Facing Direction:** Write the facing direction.
5. **Handle Visibility:** Ensure the visibility flag is set to render the entity.
*Risk:* If you modify an NPC that has an active AI script or trigger, the game might crash or behave erratically. It is safer to find "dummy" entities or inject a new entity if the engine supports dynamic allocation (though hijacking is simpler via RAM).

## 4. CTRPluginFramework (.3gx) Capabilities
Yes, CTRPluginFramework can write to these NPC structs in real-time. It provides `Process::Write32` or `Process::WriteFloat` functions. A .3gx plugin running on Azahar (both PC and Android) can continuously run a background thread to read the remote player coordinates and overwrite the hijacked NPC's coordinate memory.

## 5. Brendan/May Model IDs
- **May (Standard):** 171
- **Brendan (Standard):** 172
- **Contest May:** 173
- **Contest Brendan:** 174
- **Bicycle May:** 316
- **Bicycle Brendan:** 317

## 6. Existing Multiplayer Mods
There is no mainstream precedent for a fully rendered 3D MMO mod for ORAS. Most multiplayer mods for 3DS Pokémon games are limited to UI changes, battle modifications, or standard local/online trading and battling.

## Caveats and Risks
- **Randomized ROMs:** RomFS and ExeFS modifications in randomized ROMs might shift the code segments. Pattern scanning is crucial.
- **Save Corruption:** Real-time memory modification of entity data carries a risk of corrupting the save state if the game saves while an entity is in an invalid state.
- **Game Crashes:** Changing model IDs dynamically might cause a crash if the game hasn't loaded the respective model assets into memory for that map.
