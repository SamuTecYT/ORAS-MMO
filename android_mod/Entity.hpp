#pragma once
#include <CTRPluginFramework.hpp>
#include <vector>

using namespace CTRPluginFramework;

#pragma pack(push, 1)
struct PlayerPacket {
    uint8_t type;       // B
    uint8_t player_id;  // B
    uint16_t model_id;  // B B (2 bytes)
    uint32_t map_id;    // I
    float x;            // f
    float y;            // f
    float z;            // f
    uint8_t facing;     // B
    uint8_t anim;       // B
    uint8_t btype;      // B
    uint8_t flags;      // B
    uint8_t lvls[6];    // 6s
    uint8_t padding[34];// 34s
};
#pragma pack(pop)

// Represents an entity block in memory (0x108 bytes)
struct OverworldEntity {
    uint32_t entityID;         // 0x00
    uint32_t modelID;          // 0x04
    float x;                   // 0x08
    float y;                   // 0x0C
    float z;                   // 0x10
    uint32_t facingDirection;  // 0x14
    uint32_t visibilityFlag;   // 0x18
    // padding to 0x108...
};

class EntityManager {
private:
    u32 base_address = 0;
    
public:
    EntityManager() {}
    
    void set_base_address(u32 addr) {
        base_address = addr;
    }

    u32 get_entity_addr(int index) {
        if (!base_address) return 0;
        // Each entity is 0x108 bytes long. We offset by 16 to avoid overwriting scripted map NPCs (slots 0-15).
        return base_address + ((index + 16) * 0x108);
    }

    // Thread-safe memory injection using CTRPluginFramework's Process writes
    void update_remote_player(int slot, const PlayerPacket& pkt) {
        u32 addr = get_entity_addr(slot);
        if (!addr) return;

        // Write Coordinates
        if (Process::CheckAddress(addr)) {
            Process::WriteFloat(addr + 0x08, pkt.x);
            Process::WriteFloat(addr + 0x0C, pkt.y);
            Process::WriteFloat(addr + 0x10, pkt.z);

            // Write Model ID (May = 171, Brendan = 172)
            Process::Write32(addr + 0x04, pkt.model_id);

            // Write Visibility (1 = visible)
            Process::Write32(addr + 0x18, 1);

            // Write Facing Direction
            Process::Write32(addr + 0x14, pkt.facing);
        }
    }

    // Hide a disconnected/ghost player by clearing visibility flag
    void hide_player(int slot) {
        u32 addr = get_entity_addr(slot);
        if (!addr) return;
        Process::Write32(addr + 0x18, 0); // 0 = invisible
    }

    // Completely clear a player slot (used for safe shutdown/save)
    void clear_player(int slot) {
        u32 addr = get_entity_addr(slot);
        if (!addr) return;
        if (Process::CheckAddress(addr)) {
            Process::Write32(addr + 0x18, 0); // visibility=0
            Process::Write32(addr + 0x04, 0); // modelID=0
        }
    }
};
