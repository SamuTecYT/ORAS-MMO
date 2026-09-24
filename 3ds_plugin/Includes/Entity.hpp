#pragma once
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>

using namespace CTRPluginFramework;

#define MAX_MMO_PLAYERS 6

#define MMO_PKT_POSITION   0x01
#define MMO_PKT_EMOTE      0x02
#define MMO_PKT_CHAT       0x03
#define MMO_PKT_BATTLE     0x04
#define MMO_PKT_EVENT_REQ  0x20
#define MMO_PKT_EVENT_RESP 0x21
#define MMO_PKT_EVENT_END  0x22
#define MMO_PKT_PSS_ACTION 0x30
#define MMO_PKT_PING       0xFF

#define PSS_MODE_NONE      0
#define PSS_MODE_BATTLE    1
#define PSS_MODE_TRADE     2

#define PSS_ACT_ATTACK        1
#define PSS_ACT_HEAL          2
#define PSS_ACT_FLEE          3
#define PSS_ACT_TRADE_OFFER   4
#define PSS_ACT_TRADE_CONFIRM 5
#define PSS_ACT_TRADE_CANCEL  6

#pragma pack(push, 1)
struct PlayerPacket {
    uint8_t  type;         // 0x01 = POSITION, 0x02 = EMOTE, 0x03 = CHAT, 0x20 = EVENT_REQ, 0x21 = EVENT_RESP, 0xFF = PING
    uint8_t  player_id;    // 1..6
    uint16_t model_id;     // 171 (May), 172 (Brendan), etc.
    uint32_t map_id;       // Current map/zone ID
    float    x;            // World X coordinate (East-West)
    float    y;            // World Y coordinate (North-South)
    float    z;            // World Z coordinate (Height / Elevation)
    uint8_t  facing;       // Facing direction (0..3)
    uint8_t  anim;         // Sub-ID (Chat ID 1..8, Emote ID 1..5, or Target Player ID for events)
    uint8_t  btype;        // Event type (1=Battle, 2=Trade, or Response 1=Accept, 2=Decline)
    uint8_t  flags;        // Status flags
    uint8_t  lvls[6];      // Party levels
    char     nickname[14]; // Custom player nickname (up to 13 chars + null terminator)
    char     msg[20];      // Custom chat text message (up to 19 chars + null terminator)
};
#pragma pack(pop)

static_assert(sizeof(PlayerPacket) == 64, "PlayerPacket must be exactly 64 bytes!");

// Estructura de Entidad en el Overworld de Pokemon ORAS
// Offset +0x04: Model ID (u32, 171 Aura / 172 Bruno)
// Offset +0x08: Flags (u32, 0x03 = visible y solido)
// Offset +0x0C: X (float)
// Offset +0x10: Altura / Z (float)
// Offset +0x14: Y (float)

struct HijackedNpc {
    u32   base_addr;
    float orig_x;
    float orig_z;
    float orig_y;
    u32   orig_model;
    bool  valid;
};

class EntityManager {
private:
    HijackedNpc m_slots[MAX_MMO_PLAYERS];

public:
    EntityManager() {
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            m_slots[i].base_addr  = 0;
            m_slots[i].orig_x     = 0.0f;
            m_slots[i].orig_z     = 0.0f;
            m_slots[i].orig_y     = 0.0f;
            m_slots[i].orig_model = 0;
            m_slots[i].valid      = false;
        }
    }

    void set_npc_slot(int index, u32 addr) {
        if (index < 0 || index >= MAX_MMO_PLAYERS) return;
        if (addr == 0 || !Process::CheckAddress(addr, MEMPERM_READ)) return;

        // Si ya teniamos un NPC diferente en este slot, restaurarlo
        if (m_slots[index].valid && m_slots[index].base_addr != addr) {
            restore_slot(index);
        }

        // Si ya esta registrado exactamente en este slot, no sobreescribir orig
        if (m_slots[index].valid && m_slots[index].base_addr == addr) {
            return;
        }

        // Guardar estado original del NPC antes de metamorfosearlo
        float ox = 0, oz = 0, oy = 0;
        u32 omodel = 0;
        Process::ReadFloat(addr + 0x0C, ox);
        Process::ReadFloat(addr + 0x10, oz);
        Process::ReadFloat(addr + 0x14, oy);
        Process::Read32(addr + 0x04, omodel);

        m_slots[index].base_addr  = addr;
        m_slots[index].orig_x     = ox;
        m_slots[index].orig_z     = oz;
        m_slots[index].orig_y     = oy;
        m_slots[index].orig_model = omodel;
        m_slots[index].valid      = true;
    }

    u32 get_entity_addr(int index) const {
        if (index >= 0 && index < MAX_MMO_PLAYERS && m_slots[index].valid) {
            return m_slots[index].base_addr;
        }
        return 0;
    }

    int get_slot_count() const {
        int c = 0;
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            if (m_slots[i].valid) c++;
        }
        return c;
    }

    // Restaura el slot de forma segura sin tocar la memoria RAM del juego
    void restore_slot(int index) {
        if (index < 0 || index >= MAX_MMO_PLAYERS) return;
        m_slots[index].valid = false;
        m_slots[index].base_addr = 0;
    }

    // Oculta a un jugador remoto (neutralizado: OSD maneja todo)
    void hide_player(int slot) {
        return;
    }

    void clear_player(int slot) {
        restore_slot(slot);
    }

    void clear_all() {
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            restore_slot(i);
        }
    }

    // Metamorfosis neutralizada (v10.9: visualización 100% en OSD sin alterar entidades del juego)
    int metamorphose_all(const PlayerPacket* packets, const bool* active, u32 current_map_id) {
        return 0;
    }

    // Inyeccion neutralizada: el renderizado es manejado de forma nativa por el motor gráfico OSD
    void render_remote_players(const PlayerPacket* packets, const bool* active, u32 current_map_id) {
        return;
    }
};
