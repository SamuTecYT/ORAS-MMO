/*
 * Protocol.hpp — ORAS MMO v13
 *
 * Paquete fijo de 64 bytes, little-endian. El layout de los campos es IDÉNTICO al de v12.6
 * (el servidor y los .bat no cambian). v13 usa campos que v12.6 dejaba sin uso:
 *   - lvls[]  -> yaw, versión de protocolo, estado de movimiento y número de secuencia
 *   - msg[]   -> en paquetes POSITION/PING: reloj del emisor en ms (para interpolar sin saltos)
 */
#pragma once
#include <3ds.h>
#include <cstdint>
#include <cstring>

#define MMO_VERSION_STR      "17.0"
#define MMO_PROTOCOL_VERSION 16

// Servidor y sala por defecto. El código público trae 127.0.0.1 (el propio PC: `python server/server.py`), así que no
// lleva la dirección de nadie. Para tu propia comunidad copia ServerConfig.example.hpp como ServerConfig.local.hpp
// (git lo ignora) y pon ahí tu servidor: tu compilación lo usará sin que llegue nunca al repositorio. Cada jugador puede
// cambiarlo igualmente con mmo_server.txt ("IP" o "IP:puerto") o desde el menú SELECT -> Conexión.
#if defined(__has_include)
#  if __has_include("ServerConfig.local.hpp")
#    include "ServerConfig.local.hpp"
#  endif
#endif
#ifndef MMO_DEFAULT_SERVER
#define MMO_DEFAULT_SERVER   "127.0.0.1"
#endif
#ifndef MMO_DEFAULT_PORT
#define MMO_DEFAULT_PORT     9000
#endif
#ifndef MMO_DEFAULT_ROOM
#define MMO_DEFAULT_ROOM     "1234"
#endif

#define MAX_MMO_PLAYERS 12      // ids 1..12 asignados por el servidor
#define MMO_PACKET_SIZE 64

// Tipos de paquete
#define MMO_PKT_POSITION   0x01
#define MMO_PKT_EMOTE      0x02
#define MMO_PKT_CHAT       0x03
#define MMO_PKT_EVENT_REQ  0x20
#define MMO_PKT_EVENT_RESP 0x21
#define MMO_PKT_EVENT_END  0x22
#define MMO_PKT_LEAVE      0x23   // lo genera el servidor cuando alguien se desconecta
#define MMO_PKT_PING       0xFF   // latido (también durante combates)
#define MMO_PKT_STATS      0x31   // v14: progreso para el ranking (en msg[])
#define MMO_PKT_BATTLE     0x32   // v14: combate en curso para que los amigos lo vean (en msg[])
#define MMO_PKT_RANK_SNAPSHOT 0x33 // v16: lo envía el servidor al entrar: ranking guardado (también desconectados)
#define MMO_PKT_KEY        0x34   // v17.0 r4: msg[0..15] = huella de tu llave del ranking (solo al servidor, PlayerKey)
#define MMO_PKT_NAME_TAKEN 0x35   // v17.0 r4: el servidor avisa de que tu nombre ya es de otro jugador

// Bits de PlayerPacket::flags
#define MMO_FLAG_IN_BATTLE   0x01
#define MMO_FLAG_TRANSITION  0x02   // cruzando una puerta / cargando mapa
#define MMO_FLAG_HIDDEN      0x04   // el jugador pidió ocultarse
#define MMO_FLAG_OFFLINE     0x80   // RANK_SNAPSHOT: el jugador no está conectado
#define MMO_FLAG_BIKE_ALT    0x08   // v16: con MOVE_BIKE, la bici de forma 2 (sin bit = forma 1)
#define MMO_FLAG_MENU        0x10   // v16.5: está en un menú (X, Y, Mochila, Pokédex...): sigue visible en su sitio
// Forma del juego -> bici: 1 = Mach, 2 = Acro [I: confirmar en la prueba en vivo; si es al revés, cambiar aquí]
#define MMO_FORM_ACRO        2

// Estados de movimiento (PlayerPacket::anim en paquetes POSITION)
enum MoveState : uint8_t {
    MOVE_IDLE = 0,
    MOVE_WALK = 1,
    MOVE_RUN  = 2,
    MOVE_BIKE = 3,
    MOVE_SURF = 4,
};

// Modelos (PlayerPacket::model_id)
#define MMO_MODEL_BRENDAN 172
#define MMO_MODEL_MAY     171

#pragma pack(push, 1)
struct PlayerPacket {
    uint8_t  type;
    uint8_t  player_id;    // el servidor lo sobrescribe con el id real del emisor
    uint16_t model_id;     // 171 Aura (May) / 172 Bruno (Brendan)
    uint32_t map_id;       // bits 0-15: área (Situation+0x00), bits 16-31: zona (Situation+0x02)
    float    x;            // este-oeste (unidades del mundo; 18 = 1 casilla)
    float    y;            // norte-sur
    float    z;            // altura
    uint8_t  facing;       // 0 S, 1 N, 2 O, 3 E (compatibilidad v12.6)
    uint8_t  anim;         // POSITION: MoveState | CHAT: id (255 = libre) | EMOTE: id | EVENT_*: id destino
    uint8_t  btype;        // EVENT_*: 1 combate / 2 intercambio; EVENT_RESP: 1 acepta / 2 rechaza
    uint8_t  flags;        // MMO_FLAG_*
    uint8_t  lvls[6];      // v13: [0..1] yaw u16, [2] versión protocolo, [3] variante de color, [4..5] secuencia u16
    char     nickname[14];
    char     msg[20];      // CHAT: texto | POSITION/PING: [0..3] reloj del emisor (ms)
};
#pragma pack(pop)

static_assert(sizeof(PlayerPacket) == MMO_PACKET_SIZE, "PlayerPacket debe medir 64 bytes");

namespace Proto {
    inline uint16_t ZoneOf(uint32_t map_id) { return (uint16_t)(map_id >> 16); }
    inline uint16_t AreaOf(uint32_t map_id) { return (uint16_t)(map_id & 0xFFFF); }
    inline uint32_t MakeMapId(uint16_t area, uint16_t zone) { return (uint32_t)area | ((uint32_t)zone << 16); }

    inline void     SetYaw(PlayerPacket &p, uint16_t yaw) { p.lvls[0] = yaw & 0xFF; p.lvls[1] = yaw >> 8; }
    inline uint16_t GetYaw(const PlayerPacket &p) { return (uint16_t)(p.lvls[0] | (p.lvls[1] << 8)); }
    inline void     SetVersion(PlayerPacket &p) { p.lvls[2] = MMO_PROTOCOL_VERSION; }
    inline uint8_t  GetVersion(const PlayerPacket &p) { return p.lvls[2]; }
    inline void     SetSeq(PlayerPacket &p, uint16_t s) { p.lvls[4] = s & 0xFF; p.lvls[5] = s >> 8; }
    inline uint16_t GetSeq(const PlayerPacket &p) { return (uint16_t)(p.lvls[4] | (p.lvls[5] << 8)); }
    inline void     SetStamp(PlayerPacket &p, uint32_t ms) { memcpy(p.msg, &ms, 4); }
    inline uint32_t GetStamp(const PlayerPacket &p) { uint32_t v; memcpy(&v, p.msg, 4); return v; }

    // Copia segura de texto a un campo fijo (siempre termina en NUL)
    inline void CopyText(char *dst, size_t cap, const char *src) {
        if (!cap) return;
        size_t n = src ? strnlen(src, cap - 1) : 0;
        memcpy(dst, src, n);
        memset(dst + n, 0, cap - n);
    }
}
