/*
 * main.cpp — ORAS MMO Plugin v12.6 (Inversión Eje Y Corregida, Fix HUD Amigo y Estabilidad Total)
 *
 * Novedades de v12.6:
 * 1. CORRECCIÓN DEFINITIVA DE INVERSIÓN DEL EJE Y (MOVIMIENTO VERTICAL 100% SINCRONIZADO):
 *    - Signo vertical corregido en la proyección espacial: al caminar al Norte (arriba), el avatar se mueve hacia arriba.
 *    - Radar top screen y orientación angular de avance (atan2) sincronizados perfectamente con el eje Y.
 *    - Ahora ambos jugadores pueden seguirse mutuamente de manera 100% intuitiva y natural.
 * 2. FIX DE CARGA DEL HUD PARA EL AMIGO (ESTABILIDAD TOTAL DE INTERFAZ):
 *    - Eliminada la condición excluyente !s_isBottomNavActive en la pantalla superior.
 *    - El HUD, coordenadas, nombres y avatares ahora cargan SIEMPRE por defecto en el overworld para todos los jugadores.
 *    - Detección positiva y limpia al abrir Combates, Bolsa de Objetos o Menú de Equipo Pokémon.
 * 3. MODO 100% ESTABLE Y NO-INVASIVO:
 *    - Cero modificaciones a la memoria de código (.text) del juego para garantizar cero excepciones y cero crasheos.
 */
#include <3ds.h>
#include "csvc.h"
#include <CTRPluginFramework.hpp>
#include <vector>
#include <string>
#include <mutex>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "NetworkClient.hpp"
#include "Entity.hpp"

using namespace CTRPluginFramework;

// ============================================================
// Estado global
// ============================================================
NetworkClient  g_client;
EntityManager  g_entityManager;
Mutex          g_playerMutex;

std::string    g_serverIP    = "127.0.0.1";
std::string    g_roomCode    = "1234";
std::string    g_playerIdStr = "?";
uint8_t        g_playerId    = 0;

std::string    g_playerName  = "Jugador";
std::string    g_playerNames[MAX_MMO_PLAYERS];

bool           g_isRunning   = true;
std::string    g_osdMsg      = "MMO v12.6: Listo";
std::string    g_chatOverlay = "";
u64            g_chatTime    = 0;

// Sistema de Invitaciones a Eventos (PSS Original)
bool           g_hasIncomingEvent     = false;
uint8_t        g_incomingSender       = 0;
uint8_t        g_incomingType         = 0; // 1 = Combate, 2 = Intercambio
std::string    g_incomingName         = "";

bool           g_eventActive          = false;
std::string    g_activeEventPartner   = "";
uint8_t        g_activeEventType      = 0;

// Posición y visualización del Radar (0 = Abajo-Izq [Despejado], 1 = Arriba-Der, 2 = Oculto)
int            g_radarPos             = 0;
bool           g_showHUD              = true; // Control para mostrar/ocultar el HUD (Nickname, Sala, Coordenadas)
static bool    s_isBottomNavActive    = true; // Estado de la subpantalla (PokéNav Plus activo vs Menú/PC/Bolsa)

// Bocadillos de Chat/Emote individuales para el Motor de Avatares 3D (WSEP)
std::string    g_playerChatMsg[MAX_MMO_PLAYERS];
u64            g_playerChatTime[MAX_MMO_PLAYERS] = {0};

// Escala de Proyección Isométrica 3D Calibrada (1:1 con la cámara nativa de ORAS para anular el efecto imán)
float          g_projScaleX = 1.25f;
float          g_projScaleY = 0.95f;
float          g_projScaleZ = 0.75f;
int            g_scaleMode  = 0; // 0 = Calibrado 1:1 Oficial (1.25x0.95), 1 = Zoom Medio (1.80x1.40), 2 = Panorámico (0.90x0.70)

// Tabla de Coordenadas del Mapa de Hoenn (AreaNav en Pantalla Inferior 320x240)
struct HoennMapPin {
    u16 map_id;
    const char* name;
    int px; // Coordenada X exacta en pantalla táctil AreaNav
    int py; // Coordenada Y exacta en pantalla táctil AreaNav
};

static const HoennMapPin HOENN_LOCATIONS[] = {
    // ── Ciudades y Pueblos (exteriors con map_id propio) ──────────────────────────
    { 223, "Littleroot Town",  85, 174 }, // Villa Raiz (0xDF)
    { 228, "Oldale Town",      85, 153 }, // Pueblo Escaso (0xE4)
    { 232, "Dewford Town",     50, 190 }, // Pueblo Azuliza (0xE8)
    { 238, "Lavaridge Town",   95,  78 }, // Pueblo Lavacalda (0xEE)
    { 240, "Fallarbor Town",   71,  46 }, // Pueblo Pardal (0xF0)
    { 246, "Verdanturf Town",  85, 110 }, // Pueblo Verdegal (0xF6)
    { 253, "Pacifidlog Town", 180, 152 }, // Pueblo Oromar (0xFD)
    { 259, "Petalburg City",   52, 153 }, // Ciudad Petalia (0x103)
    { 265, "Slateport City",  130, 165 }, // Ciudad Portual (0x109)
    { 278, "Mauville City",   130, 108 }, // Ciudad Malvalona (0x116)
    { 284, "Rustboro City",    41, 104 }, // Ciudad Ferrica (0x11C)
    { 301, "Fortree City",    165,  58 }, // Ciudad Arborada (0x12D)
    { 309, "Lilycove City",   196,  78 }, // Ciudad Calagua (0x135)
    { 330, "Mossdeep City",   226,  92 }, // Ciudad Algaria (0x14A)
    { 341, "Sootopolis City", 192, 112 }, // Arrecipolis (0x155)
    { 353, "Ever Grande City",232, 142 }, // Ciudad Colosalia (0x161)
    { 364, "Pokemon League",  232, 126 }, // Liga Pokemon (0x16C)
    { 518, "Battle Resort",   205, 170 }, // Resort Batalla (0x206)

    // ── Interiores de edificios (IDs pequenos, ORAS/RSE) ─────────────────────────
    {   4, "Mauville City",   130, 108 }, // Centro Comercial Mauville (interior)
    // Gimnasios (IDs 26-33) y salas de la Liga (34-38)
    {  26, "Rustboro City",    41, 104 }, // Gimnasio Rustboro (Roxanne)
    {  27, "Dewford Town",     50, 190 }, // Gimnasio Dewford (Brawly)
    {  28, "Mauville City",   130, 108 }, // Gimnasio Mauville (Wattson)
    {  29, "Lavaridge Town",   95,  78 }, // Gimnasio Lavaridge (Flannery)
    {  30, "Petalburg City",   52, 153 }, // Gimnasio Petalburg (Norman)
    {  31, "Fortree City",    165,  58 }, // Gimnasio Fortree (Winona)
    {  32, "Mossdeep City",   226,  92 }, // Gimnasio Mossdeep (Tate & Liza)
    {  33, "Sootopolis City", 192, 112 }, // Gimnasio Sootopolis (Juan / Wallace)
    {  34, "Pokemon League",  232, 126 }, // Sala Elite 4 #1
    {  35, "Pokemon League",  232, 126 }, // Sala Elite 4 #2
    {  36, "Pokemon League",  232, 126 }, // Sala Elite 4 #3
    {  37, "Pokemon League",  232, 126 }, // Sala Elite 4 #4
    {  38, "Pokemon League",  232, 126 }, // Sala del Campeon

    // ── Mazmorras y Cuevas (IDs ORAS compatibles con RSE para Gen-3 locations) ───
    // Cascada Meteoro — Ruta 114 (noroeste de Hoenn)
    {  71, "Meteor Falls",     58,  64 },
    {  72, "Meteor Falls",     58,  64 },
    {  73, "Meteor Falls",     58,  64 },
    {  74, "Meteor Falls",     58,  64 },
    // Cueva Granito — sur de Dewford, Ruta 106
    {  78, "Granite Cave",     45, 185 },
    {  79, "Granite Cave",     45, 185 },
    {  80, "Granite Cave",     45, 185 },
    // Bosque Petalia — Ruta 104 (entre Rustboro y Petalburg)
    {  82, "Petalburg Woods",  41, 132 },
    // Tunel Rugido — entre Verdanturf y Rustboro (Ruta 116)
    {  83, "Rusturf Tunnel",   65, 107 },
    // Barco Abandonado — Ruta 108, oceano sur
    {  84, "Abandoned Ship",   95, 190 },
    {  85, "Abandoned Ship",   95, 190 },
    // Ruinas del Desierto — Ruta 111 (desierto norte)
    {  86, "Desert Ruins",    120,  84 },
    // Cueva Isla — Ruta 105 (oceano oeste)
    {  87, "Island Cave",      41, 168 },
    // Tumba Antigua — Ruta 120 (noreste)
    {  88, "Ancient Tomb",    178,  64 },
    // Nueva Malvalona — bajo Ruta 110 (este de Mauville)
    {  90, "New Mauville",    148, 108 },
    {  91, "New Mauville",    148, 108 },
    // Senda Ardiente y Monte Chimenea — Rutas 112-113
    {  92, "Fiery Path",      108,  80 },
    {  93, "Mt. Chimney",     108,  62 },
    {  94, "Jagged Pass",     105,  70 },
    {  95, "Jagged Pass",     105,  70 },
    // Monte Pira — Ruta 122 (noreste de Lilycove)
    {  96, "Mt. Pyre",        180,  95 },
    {  97, "Mt. Pyre",        180,  95 },
    {  98, "Mt. Pyre",        180,  95 },
    {  99, "Mt. Pyre",        180,  95 },
    { 100, "Mt. Pyre",        180,  95 },
    { 101, "Mt. Pyre",        180,  95 },
    // Guaridas Team Aqua / Team Magma
    { 103, "Team Aqua Hideout",200, 78 },
    { 104, "Team Magma Hideout",110,62 },
    { 105, "Team Magma Hideout",110,62 },
    // Cueva Fondo del Mar — Ruta 128 (oceano sureste)
    { 106, "Seafloor Cavern", 218, 130 },
    { 107, "Seafloor Cavern", 218, 130 },
    { 108, "Seafloor Cavern", 218, 130 },
    { 109, "Seafloor Cavern", 218, 130 },
    { 110, "Seafloor Cavern", 218, 130 },
    // Cueva Origen — Sootopolis City
    { 111, "Cave of Origin",  192, 112 },
    { 112, "Cave of Origin",  192, 112 },
    // Camino Victoria — Ever Grande City
    { 113, "Victory Road",    232, 140 },
    { 114, "Victory Road",    232, 140 },
    { 115, "Victory Road",    232, 140 },
    { 116, "Victory Road",    232, 140 },
    // Cueva Shoal — Ruta 125 (norte de Mossdeep)
    { 118, "Shoal Cave",      226,  76 },
    { 119, "Shoal Cave",      226,  76 },
    { 120, "Shoal Cave",      226,  76 },
    { 121, "Shoal Cave",      226,  76 },
    // Camara Sellada — Ruta 134 (oceano sur)
    { 122, "Sealed Chamber",  150, 160 },
    // Pilar del Cielo — Ruta 131 (oceano sureste)
    { 123, "Sky Pillar",      208, 155 },
    { 124, "Sky Pillar",      208, 155 },
    { 125, "Sky Pillar",      208, 155 },
    { 126, "Sky Pillar",      208, 155 },
    // Zona Safari — Lilycove City
    { 127, "Safari Zone",     196,  78 },
    { 128, "Safari Zone",     196,  78 },
    { 129, "Safari Zone",     196,  78 },
    { 130, "Safari Zone",     196,  78 },
    { 131, "Safari Zone",     196,  78 },
    // Sea Mauville — Ruta 108 (barco hundido al sur)
    { 132, "Sea Mauville",    100, 190 },
    { 133, "Sea Mauville",    100, 190 },
    { 134, "Sea Mauville",    100, 190 },
    { 135, "Sea Mauville",    100, 190 },
    { 136, "Sea Mauville",    100, 190 },
    // Isla Meridional
    { 137, "Southern Island", 165, 175 },

    // ── Rutas de Hoenn (IDs ficticios 9101+ — solo para lookup por proximidad de pixel) ─
    { 9101, "Route 101",        85, 164 },
    { 9102, "Route 102",        68, 153 },
    { 9103, "Route 103",        85, 138 },
    { 9104, "Route 104",        46, 138 },
    { 9105, "Route 105",        41, 168 },
    { 9106, "Route 106",        46, 185 },
    { 9107, "Route 107",        70, 190 },
    { 9108, "Route 108",        95, 190 },
    { 9109, "Route 109",       125, 185 },
    { 9110, "Route 110",       130, 136 },
    { 9111, "Route 111",       130,  78 },
    { 9112, "Route 112",       112,  78 },
    { 9113, "Route 113",        85,  46 },
    { 9114, "Route 114",        60,  55 },
    { 9115, "Route 115",        41,  80 },
    { 9116, "Route 116",        56, 104 },
    { 9117, "Route 117",       107, 110 },
    { 9118, "Route 118",       148, 108 },
    { 9119, "Route 119",       165,  78 },
    { 9120, "Route 120",       178,  68 },
    { 9121, "Route 121",       186,  78 },
    { 9122, "Route 122",       180,  95 },
    { 9123, "Route 123",       150, 115 },
    { 9124, "Route 124",       211,  92 },
    { 9125, "Route 125",       226,  78 },
    { 9126, "Route 126",       192, 118 },
    { 9127, "Route 127",       226, 112 },
    { 9128, "Route 128",       226, 135 },
    { 9129, "Route 129",       220, 158 },
    { 9130, "Route 130",       200, 155 },
    { 9131, "Route 131",       205, 158 },
    { 9132, "Route 132",       175, 162 },
    { 9133, "Route 133",       162, 162 },
    { 9134, "Route 134",       148, 162 }
};

// Cola de paquetes personalizados (Chat, Emote, Eventos PSS)
PlayerPacket   g_pendingCustomPkt;
bool           g_hasPendingCustomPkt  = false;

// Cache de paquetes remotos (para renderizado continuo en OnFrameCallback)
PlayerPacket   g_latestRemotePackets[MAX_MMO_PLAYERS];
bool           g_remotePlayerActive[MAX_MMO_PLAYERS] = {false};
u64            g_lastPacketTimeMs[MAX_MMO_PLAYERS]   = {0};

// Datos del jugador local (Offsets de Pokemon ORAS)
u32 local_player_base   = 0;
u32 local_player_x      = 0;
u32 local_player_z      = 0;
u32 local_player_y      = 0;
u32 local_player_facing = 0;
u16 local_model_id      = 171; // 171 = Aura, 172 = Bruno

// Posiciones cacheadas del jugador local para el radar OSD
float g_localX = 0.0f;
float g_localZ = 0.0f;
float g_localY = 0.0f;
u16   g_currentMapId = 0;

// Mensajes de Chat Rapido
struct ChatMsg { const char* texto; uint8_t id; };
static const ChatMsg CHAT_MSGS[] = {
    { "¡Hola a todos!",      0x01 }, { "¡Estoy aqui!",       0x02 },
    { "¡Vamos a batallar!",  0x03 }, { "¡Sigueme!",          0x04 },
    { "¡Mira este Pokemon!", 0x05 }, { "¿Intercambiamos?",   0x06 },
    { "¡GG!",                0x07 }, { "¡Hasta luego!",      0x08 },
};

static std::string GetPlayerDisplayName(int pid) {
    int slot = pid - 1;
    if (slot >= 0 && slot < MAX_MMO_PLAYERS && !g_playerNames[slot].empty()) {
        return g_playerNames[slot] + " (J" + std::to_string(pid) + ")";
    }
    return "J" + std::to_string(pid);
}

// ============================================================
// Detección de Transiciones de Escena, Puertas, Combates y Menús (v12.6)
// ============================================================
static inline bool IsSceneTransitionActive() {
    // 1. Verificación de Transición de Puerta / Teletransporte (ORAS Native Door Warp Check)
    // Cuando el jugador cruza una puerta (ej. Centro Pokémon), 0x08803C20 contiene 0x5544
    u16 warpCheck = 0;
    if (Process::CheckAddress(0x08803C20, MEMPERM_READ) && Process::Read16(0x08803C20, warpCheck)) {
        if (warpCheck == 0x5544) return true;
    }
    // 2. Verificación de Combate Activo (0x40001)
    u32 battleState = 0;
    if (Process::CheckAddress(0x081FB478, MEMPERM_READ) && Process::Read32(0x081FB478, battleState)) {
        if (battleState == 0x40001) return true;
    }
    // 3. Verificación de Pantalla de Intercambio PSS (0x5544)
    u32 tradeCheck = 0;
    if (Process::CheckAddress(0x08523D14, MEMPERM_READ) && Process::Read32(0x08523D14, tradeCheck)) {
        if (tradeCheck == 0x5544) return true;
    }
    return false;
}

// ============================================================
// Descubrimiento de Entidades (v10.9: Neutralizado para 100% de estabilidad)
// Los jugadores se renderizan exclusivamente en OSD; jamás se alteran entidades del juego
// ============================================================
static void DiscoverEntitySlots() {
    // Modo 100% No-Invasivo: Cero escrituras en la RAM del juego
    return;
}

// ============================================================
// Sincronizacion Inteligente de Coordenadas (Paso 1 y Paso 2)
// ============================================================
struct CoordSnap {
    u32   addr;
    float x;
    float z;
    float y;
};
static const int MAX_SNAPS = 256;
static CoordSnap g_coordSnaps[MAX_SNAPS];
static int       g_coordCount = 0;

static void MenuLinkPlayer1(MenuEntry *e) {
    g_coordCount = 0;
    local_player_base = 0;
    local_player_x    = 0;
    local_player_z    = 0;
    local_player_y    = 0;

    if (Process::CheckAddress(0x08C6E894, MEMPERM_READ)) {
        float sx = 0, sz = 0, sy = 0;
        Process::ReadFloat(0x08C6E894, sx);
        Process::ReadFloat(0x08C6E898, sz);
        Process::ReadFloat(0x08C6E89C, sy);
        g_coordSnaps[g_coordCount++] = {0x08C6E894, sx, sz, sy};
    }

    u32 cur_page = 0xFFFFFFFF;
    bool page_valid = false;

    for (u32 addr = 0x08C60000; addr < 0x08D60000 && g_coordCount < MAX_SNAPS; addr += 4) {
        u32 page = addr & ~0xFFF;
        if (page != cur_page) {
            cur_page = page;
            page_valid = Process::CheckAddress(page, MEMPERM_READ);
        }
        if (!page_valid) {
            addr += 0x1000 - 4;
            continue;
        }

        float x = 0, z = 0, y = 0;
        if (Process::ReadFloat(addr, x) &&
            Process::ReadFloat(addr + 4, z) &&
            Process::ReadFloat(addr + 8, y))
        {
            if (std::isfinite(x) && std::isfinite(z) && std::isfinite(y)) {
                if ((std::abs(x) > 0.01f || std::abs(z) > 0.01f || std::abs(y) > 0.01f) &&
                    std::abs(x) < 20000.0f && std::abs(z) < 20000.0f && std::abs(y) < 20000.0f)
                {
                    if (addr != 0x08C6E894) {
                        g_coordSnaps[g_coordCount++] = {addr, x, z, y};
                    }
                }
            }
        }
    }

    char buf[220];
    snprintf(buf, sizeof(buf), 
        "✓ ¡Paso 1 completado con exito!\n\n"
        "Candidatos registrados: %d\n\n"
        "1. Presiona B para volver al juego.\n"
        "2. CAMINA 3 o 4 pasos.\n"
        "3. Detente, abre SELECT y pulsa Paso 2.", 
        g_coordCount);
    MessageBox("Calibracion de Coordenadas", buf)();
}

static void MenuLinkPlayer2(MenuEntry *e) {
    if (g_coordCount == 0) {
        MessageBox("Error", "Debes presionar el Paso 1 primero.")();
        return;
    }
    
    u32   best_addr   = 0;
    u32   best_x_addr = 0;
    u32   best_y_addr = 0;
    u32   best_z_addr = 0;
    float best_dist   = 0.0f;
    float best_x      = 0.0f, best_z = 0.0f, best_y = 0.0f;
    float best_score  = -999999.0f;

    for (int i = 0; i < g_coordCount; i++) {
        const auto &snap = g_coordSnaps[i];
        if (!Process::CheckAddress(snap.addr, MEMPERM_READ)) continue;
        
        float c0 = 0, c1 = 0, c2 = 0;
        if (!Process::ReadFloat(snap.addr, c0) ||
            !Process::ReadFloat(snap.addr + 4, c1) ||
            !Process::ReadFloat(snap.addr + 8, c2))
            continue;

        if (!std::isfinite(c0) || !std::isfinite(c1) || !std::isfinite(c2))
            continue;

        float d0 = c0 - snap.x;
        float d1 = c1 - snap.z;
        float d2 = c2 - snap.y;

        float dist = std::sqrt(d0*d0 + d1*d1 + d2*d2);

        if (dist > 0.15f && dist < 60.0f) {
            float score = 100.0f - std::abs(dist - 3.2f);

            if (snap.addr == 0x08C6E894) {
                score += 100000.0f;
            }

            if (score > best_score) {
                best_score = score;
                best_addr  = snap.addr;
                best_dist  = dist;

                best_x_addr = snap.addr;
                best_z_addr = snap.addr + 4;
                best_y_addr = snap.addr + 8;
                best_x = c0; best_z = c1; best_y = c2;
            }
        }
    }
    
    if (best_addr != 0) {
        local_player_x      = best_x_addr;
        local_player_y      = best_y_addr;
        local_player_z      = best_z_addr;
        local_player_base   = (best_x_addr >= 0x0C) ? (best_x_addr - 0x0C) : 0x08C6E888;
        local_player_facing = 0x08C6E886;
        if (local_model_id == 0) local_model_id = 171; // 171 = Aura (May) por defecto
        
        g_localX = best_x;
        g_localZ = best_z;
        g_localY = best_y;

        char buf[280];
        snprintf(buf, sizeof(buf), 
            "✓ ¡CALIBRACION DEFINITIVA EXITOSA!\n\n"
            "Direccion X: 0x%08X\n"
            "Posicion en vivo: X: %.1f  Y: %.1f\n"
            "Desplazamiento: %.2fm\n\n"
            "¡Coordenadas y modelos 3D listos!", 
            best_x_addr, best_x, best_y, best_dist);
        MessageBox("Sincronizacion Multijugador", buf)();
    } else {
        char buf[260];
        snprintf(buf, sizeof(buf), 
            "No se detecto un movimiento valido de jugador.\n"
            "(Candidatos analizados: %d)\n\n"
            "1. Presiona B para volver al juego.\n"
            "2. Camina 3 pasos manteniendo el D-Pad o Joystick.\n"
            "3. Detente y vuelve a pulsar Paso 2.",
            g_coordCount);
        MessageBox("Aviso", buf)();
    }
}

static void MenuLinkAuto(MenuEntry *e) {
    local_player_base   = 0x08C6E888;
    local_player_x      = 0x08C6E894;
    local_player_z      = 0x08C6E898;
    local_player_y      = 0x08C6E89C;
    local_player_facing = 0x08C6E886;
    if (local_model_id == 0) local_model_id = 171;

    float tx = 0, tz = 0, ty = 0;
    Process::ReadFloat(local_player_x, tx);
    Process::ReadFloat(local_player_z, tz);
    Process::ReadFloat(local_player_y, ty);
    g_localX = tx; g_localZ = tz; g_localY = ty;

    char buf[240];
    snprintf(buf, sizeof(buf), 
        "✓ ¡Jugador vinculado al instante!\n\n"
        "X: %.1f  Y: %.1f  Z: %.1f\n"
        "Direccion X: 0x%08X\n"
        "Personaje: %s (%d)\n\n"
        "¡Camina en el juego para verlas moverse!",
        tx, ty, tz, (unsigned int)local_player_x,
        (local_model_id == 171) ? "Aura" : "Bruno", local_model_id);
    MessageBox("Vinculacion Instantanea", buf)();
}

static void TryAutoLinkBackground() {
    if (g_currentMapId == 2) return; // Nunca vincular en interiores del Centro Pokemon (map 2)
    if (local_player_x != 0 && Process::CheckAddress(local_player_x, MEMPERM_READ)) return;

    local_player_base   = 0x08C6E888;
    local_player_x      = 0x08C6E894;
    local_player_z      = 0x08C6E898;
    local_player_y      = 0x08C6E89C;
    local_player_facing = 0x08C6E886;
    if (local_model_id == 0) local_model_id = 171;

    Process::ReadFloat(local_player_x, g_localX);
    Process::ReadFloat(local_player_z, g_localZ);
    Process::ReadFloat(local_player_y, g_localY);
}

// ============================================================
// Envio seguro de paquetes con Nombre Personalizado
// ============================================================
static void QueueCustomPacket(uint8_t type, uint8_t anim, uint8_t btype, const char* customMsg = nullptr) {
    g_playerMutex.Lock();
    memset(&g_pendingCustomPkt, 0, sizeof(PlayerPacket));
    g_pendingCustomPkt.type      = type;
    g_pendingCustomPkt.player_id = g_playerId;
    g_pendingCustomPkt.anim      = anim;
    g_pendingCustomPkt.btype     = btype;
    g_pendingCustomPkt.model_id  = local_model_id;
    g_pendingCustomPkt.map_id    = (uint32_t)g_currentMapId;
    g_pendingCustomPkt.x         = g_localX;
    g_pendingCustomPkt.z         = g_localZ;
    g_pendingCustomPkt.y         = g_localY;
    strncpy(g_pendingCustomPkt.nickname, g_playerName.c_str(), sizeof(g_pendingCustomPkt.nickname) - 1);
    if (customMsg) {
        strncpy(g_pendingCustomPkt.msg, customMsg, sizeof(g_pendingCustomPkt.msg) - 1);
    }
    g_hasPendingCustomPkt        = true;
    g_playerMutex.Unlock();
}

// ============================================================
// Hilo de Red (TCP Ultra-Rapido con Bufer Acumulador)
// ============================================================
void NetworkThread(void *arg) {
    int retry_delay = 2;

    Sleep(Seconds(3));

    File ipFile("mmo_ip.txt", File::READ);
    if (ipFile.IsOpen()) {
        char buf[64] = {0};
        ipFile.Read(buf, 63);
        std::string ip = buf;
        while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' '))
            ip.pop_back();
        if (!ip.empty() && ip.find(' ') == std::string::npos && ip.find('.') != std::string::npos) {
            g_serverIP = ip;
        }
        ipFile.Close();
    }

    File nameFile("mmo_name.txt", File::READ);
    if (nameFile.IsOpen()) {
        char buf[32] = {0};
        nameFile.Read(buf, 31);
        std::string name = buf;
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' '))
            name.pop_back();
        if (!name.empty()) {
            g_playerName = name;
        }
        nameFile.Close();
    }

    while (g_isRunning) {
        if (!g_client.is_connected()) {
            g_osdMsg = "MMO v12.6: Conectando a " + g_serverIP + "...";
            std::string err;
            if (g_client.connect(g_serverIP, 9000, err)) {
                if (g_client.send_handshake(g_roomCode)) {
                    std::string resp = g_client.recv_handshake();
                    if (resp.find("OK:ROOM:") != std::string::npos) {
                        size_t last = resp.find_last_of(':');
                        g_playerIdStr = (last != std::string::npos) ? resp.substr(last + 1) : "?";
                        while (!g_playerIdStr.empty() && (g_playerIdStr.back() == '\r' || g_playerIdStr.back() == '\n'))
                            g_playerIdStr.pop_back();
                        g_playerId = (uint8_t)atoi(g_playerIdStr.c_str());
                        g_osdMsg = "MMO v12.6 OK | Sala:" + g_roomCode + " [" + g_playerName + "]";
                        retry_delay = 2;

                        g_playerMutex.Lock();
                        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                            g_remotePlayerActive[i] = false;
                            g_lastPacketTimeMs[i] = 0;
                        }
                        g_playerMutex.Unlock();
                        TryAutoLinkBackground();
                    } else {
                        g_osdMsg = "MMO: Error al unirse a sala";
                        g_client.disconnect();
                    }
                }
            } else {
                g_osdMsg = "MMO: " + err;
            }

            if (!g_client.is_connected()) {
                Sleep(Seconds(retry_delay));
                if (retry_delay < 20) retry_delay += 3;
                continue;
            }
        }

        if (g_client.is_connected()) {
            // Guardia: si hay transición activa (puerta o combate), esperar y no tocar memoria
            if (IsSceneTransitionActive()) {
                Sleep(Milliseconds(33));
                continue;
            }

            u16 cur_map_id = 0;
            if (Process::CheckAddress(0x08C6E884, MEMPERM_READ)) {
                Process::Read16(0x08C6E884, cur_map_id);
            }
            if (cur_map_id >= 1) {
                g_currentMapId = cur_map_id;
            }

            if (cur_map_id == 2) {
                // En interiores (Centro Pokemon): enviar latido de presencia sin borrar coordenadas calibradas
                static u32 s_interior_ticks = 0;
                if (++s_interior_ticks % 30 == 0) {
                    PlayerPacket pkt;
                    memset(&pkt, 0, sizeof(pkt));
                    pkt.type      = MMO_PKT_POSITION;
                    pkt.player_id = g_playerId;
                    pkt.model_id  = local_model_id;
                    pkt.map_id    = (uint32_t)cur_map_id;
                    pkt.x         = 0.0f;
                    pkt.y         = 0.0f;
                    pkt.z         = 0.0f;
                    pkt.facing    = 0;
                    strncpy(pkt.nickname, g_playerName.c_str(), sizeof(pkt.nickname) - 1);
                    g_client.send_packet((uint8_t*)&pkt, sizeof(PlayerPacket));
                }
            } else if (local_player_x == 0) {
                static u32 s_auto_ticks = 0;
                if (++s_auto_ticks % 30 == 0) {
                    TryAutoLinkBackground();
                }
            }

            // --- ENVIAR COORDENADAS LOCALES (TIEMPO REAL CON FILTRO DE SEGURIDAD) ---
            if (cur_map_id != 2 && local_player_x != 0 && Process::CheckAddress(local_player_x, MEMPERM_READ)) {
                float x = 0, z = 0, y = 0;
                if (Process::ReadFloat(local_player_x, x) &&
                    Process::ReadFloat(local_player_z, z) &&
                    Process::ReadFloat(local_player_y, y))
                {
                    if (std::isfinite(x) && std::isfinite(z) && std::isfinite(y) &&
                        std::abs(x) < 50000.0f && std::abs(z) < 50000.0f && std::abs(y) < 50000.0f)
                    {
                        g_localX = x;
                        g_localZ = z;
                        g_localY = y;

                        u8 facing = 0;
                        if (local_player_facing && Process::CheckAddress(local_player_facing, MEMPERM_READ)) {
                            Process::Read8(local_player_facing, facing);
                        }

                        PlayerPacket pkt;
                        memset(&pkt, 0, sizeof(pkt));
                        pkt.type      = MMO_PKT_POSITION;
                        pkt.player_id = g_playerId;
                        pkt.model_id  = local_model_id;
                        pkt.map_id    = (uint32_t)cur_map_id;
                        pkt.x         = x;
                        pkt.y         = y;
                        pkt.z         = z;
                        pkt.facing    = facing;
                        strncpy(pkt.nickname, g_playerName.c_str(), sizeof(pkt.nickname) - 1);

                        g_client.send_packet((uint8_t*)&pkt, sizeof(PlayerPacket));
                    }
                }
            }

            // --- ENVIAR PAQUETES PERSONALIZADOS ---
            if (g_hasPendingCustomPkt) {
                g_playerMutex.Lock();
                PlayerPacket to_send = g_pendingCustomPkt;
                g_hasPendingCustomPkt = false;
                g_playerMutex.Unlock();

                g_client.send_packet((uint8_t*)&to_send, sizeof(PlayerPacket));
            }

            // --- RECIBIR PAQUETES REMOTOS (CON BÚFER ACUMULADOR TCP) ---
            while (true) {
                PlayerPacket recv_pkt;
                int bytes = g_client.recv_packet_nonblock((uint8_t*)&recv_pkt, sizeof(PlayerPacket));
                if (bytes <= 0) break;

                if (bytes == sizeof(PlayerPacket)) {
                    int slot = recv_pkt.player_id - 1;

                    if (slot >= 0 && slot < MAX_MMO_PLAYERS) {
                        if (recv_pkt.nickname[0] != '\0') {
                            g_playerNames[slot] = recv_pkt.nickname;
                        }
                    }

                    std::string sender_name = (slot >= 0 && slot < MAX_MMO_PLAYERS && !g_playerNames[slot].empty())
                                              ? g_playerNames[slot] : ("J" + std::to_string(recv_pkt.player_id));

                    if (recv_pkt.type == MMO_PKT_POSITION) {
                        if (slot >= 0 && slot < MAX_MMO_PLAYERS && slot != (g_playerId - 1)) {
                            g_playerMutex.Lock();
                            g_latestRemotePackets[slot] = recv_pkt;
                            g_remotePlayerActive[slot]  = true;
                            g_lastPacketTimeMs[slot]    = osGetTime();
                            g_playerMutex.Unlock();
                        }
                    }
                    else if (recv_pkt.type == MMO_PKT_CHAT) {
                        std::string txt = "¡Hola!";
                        if (recv_pkt.anim == 255) {
                            char cleanMsg[21] = {0};
                            memcpy(cleanMsg, recv_pkt.msg, 20);
                            txt = cleanMsg;
                        } else {
                            uint8_t cid = recv_pkt.anim;
                            for (size_t c = 0; c < sizeof(CHAT_MSGS)/sizeof(CHAT_MSGS[0]); c++) {
                                if (CHAT_MSGS[c].id == cid) {
                                    txt = CHAT_MSGS[c].texto;
                                    break;
                                }
                            }
                        }
                        char cbuf[128];
                        snprintf(cbuf, sizeof(cbuf), "[%s]: %s", sender_name.c_str(), txt.c_str());
                        g_chatOverlay = cbuf;
                        g_chatTime = osGetTime();
                        if (slot >= 0 && slot < MAX_MMO_PLAYERS) {
                            g_playerChatMsg[slot]  = txt;
                            g_playerChatTime[slot] = osGetTime();
                        }
                        OSD::Notify(cbuf, Color::Yellow);
                    }
                    else if (recv_pkt.type == MMO_PKT_EMOTE) {
                        uint8_t eid = recv_pkt.anim;
                        const char* em_names[] = { "Saludar", "Saltar", "Corazon", "Interrogacion", "Exclamacion" };
                        const char* em_icons[] = { "(^o^)/", "^o^", "<3", "(?)", "(!)" };
                        const char* ename = (eid >= 1 && eid <= 5) ? em_names[eid - 1] : "Emote";
                        const char* eicon = (eid >= 1 && eid <= 5) ? em_icons[eid - 1] : "*";
                        char cbuf[128];
                        snprintf(cbuf, sizeof(cbuf), "[%s] %s: %s", sender_name.c_str(), ename, eicon);
                        g_chatOverlay = cbuf;
                        g_chatTime = osGetTime();
                        if (slot >= 0 && slot < MAX_MMO_PLAYERS) {
                            g_playerChatMsg[slot]  = eicon;
                            g_playerChatTime[slot] = osGetTime();
                        }
                        OSD::Notify(cbuf, Color(0, 220, 255));
                    }
                    // --- SISTEMA DE INVITACIONES PSS ORIGINAL ---
                    else if (recv_pkt.type == MMO_PKT_EVENT_REQ) {
                        if (recv_pkt.anim == g_playerId) {
                            g_incomingSender = recv_pkt.player_id;
                            g_incomingType   = recv_pkt.btype;
                            g_incomingName   = sender_name;
                            g_hasIncomingEvent = true;

                            const char* tname = (recv_pkt.btype == 1) ? "COMBATE" : "INTERCAMBIO";
                            char banner[140];
                            snprintf(banner, sizeof(banner), "⚔ ¡[%s] te invita a %s! Abre SELECT para responder", sender_name.c_str(), tname);
                            g_chatOverlay = banner;
                            g_chatTime = osGetTime();
                            OSD::Notify(banner, Color::Orange);
                        }
                    }
                    else if (recv_pkt.type == MMO_PKT_EVENT_RESP) {
                        if (recv_pkt.anim == g_playerId) {
                            if (recv_pkt.btype == 1) { // Aceptado
                                g_eventActive = true;
                                g_activeEventPartner = sender_name;

                                char banner[140];
                                snprintf(banner, sizeof(banner), "✓ ¡[%s] ACEPTÓ! Abre el PSS en la pantalla táctil.", sender_name.c_str());
                                g_chatOverlay = banner;
                                g_chatTime = osGetTime();
                                OSD::Notify(banner, Color::Lime);
                            } else {
                                char banner[140];
                                snprintf(banner, sizeof(banner), "✗ [%s] rechazó la invitación.", sender_name.c_str());
                                OSD::Notify(banner, Color::Red);
                            }
                        }
                    }
                    else if (recv_pkt.type == MMO_PKT_EVENT_END) {
                        if (g_eventActive) {
                            g_eventActive = false;
                            g_activeEventPartner = "";
                            OSD::Notify("Tu compañero finalizó la sesión.", Color::Orange);
                        }
                    }
                }
            }

            // --- TIMEOUT DE JUGADORES DESCONECTADOS (12 segundos en reloj real) ---
            u64 now_ms = osGetTime();
            for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                if (i == (g_playerId - 1)) continue;
                if (g_remotePlayerActive[i] && g_lastPacketTimeMs[i] > 0 &&
                    (now_ms - g_lastPacketTimeMs[i]) > 12000) 
                {
                    g_playerMutex.Lock();
                    g_remotePlayerActive[i] = false;
                    g_entityManager.hide_player(i);
                    g_playerMutex.Unlock();
                }
            }
        }

        Sleep(Milliseconds(33));
    }
}

// ============================================================
// Motor de Avatares 3D Volumétricos de Hoenn (ORAS v12.6)
// Pipeline de proyección isométrica 3D, sombreado solar direccional (N·L),
// ordenamiento por profundidad (Painter's Algorithm) y cinemática de articulación.
// Modelos de Bruno (Brendan) y Aura (May) con proporciones oficiales ORAS Chibi.
// ============================================================

struct Point2D { int x, y; };

struct ProjectedFace {
    float depth;
    Point2D p[4];
    Color col;
};

static const int MAX_3D_FACES = 96;
static ProjectedFace s_faceBuffer[MAX_3D_FACES];
static int s_faceCount = 0;

static inline void ResetFaceBuffer() {
    s_faceCount = 0;
}

// Rasterizador de cuadriláteros convexos por scanline (Cero alocación dinámica, seguro y ultra-rápido)
static inline void DrawScanlineQuad(const Screen &screen, const Point2D p[4], const Color &col) {
    int minY = p[0].y;
    int maxY = p[0].y;
    for (int i = 1; i < 4; i++) {
        if (p[i].y < minY) minY = p[i].y;
        if (p[i].y > maxY) maxY = p[i].y;
    }
    int scrMinY = 0;
    int scrMaxY = 239;
    if (minY < scrMinY) minY = scrMinY;
    if (maxY > scrMaxY) maxY = scrMaxY;
    if (minY > maxY) return;

    for (int y = minY; y <= maxY; y++) {
        int xIntersects[4];
        int count = 0;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            int y0 = p[i].y;
            int y1 = p[j].y;
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                if (y1 != y0) {
                    int x = p[i].x + (int)((float)(y - y0) * (float)(p[j].x - p[i].x) / (float)(y1 - y0));
                    xIntersects[count++] = x;
                }
            }
        }
        if (count >= 2) {
            int xMin = xIntersects[0];
            int xMax = xIntersects[1];
            if (xMin > xMax) std::swap(xMin, xMax);
            for (int k = 2; k < count; k++) {
                if (xIntersects[k] < xMin) xMin = xIntersects[k];
                if (xIntersects[k] > xMax) xMax = xIntersects[k];
            }
            int scrMaxX = screen.IsTop ? 399 : 319;
            if (xMin < 0) xMin = 0;
            if (xMax > scrMaxX) xMax = scrMaxX;
            if (xMax >= xMin) {
                screen.DrawRect((u32)xMin, (u32)y, (u32)(xMax - xMin + 1), 1, col, true);
            }
        }
    }
}

// Renderizar todas las caras acumuladas en orden de profundidad (Painter's Algorithm)
static inline void RenderSortedFaces(const Screen &screen) {
    if (s_faceCount <= 0) return;
    std::sort(s_faceBuffer, s_faceBuffer + s_faceCount, [](const ProjectedFace &a, const ProjectedFace &b) {
        return a.depth > b.depth; // De más lejano a más cercano (Back-to-Front)
    });
    for (int i = 0; i < s_faceCount; i++) {
        DrawScanlineQuad(screen, s_faceBuffer[i].p, s_faceBuffer[i].col);
    }
}

// Agrega un paralelepípedo / caja 3D orientada al búfer de caras con sombreado solar N·L
static void AddBox3D(float cx, float cy, float cz,
                     float hx, float hy, float hz,
                     float limbPitch, float bodyYaw,
                     int baseSX, int baseSY, float scale,
                     const Color &topCol, const Color &botCol,
                     const Color &frontCol, const Color &backCol,
                     const Color &rightCol, const Color &leftCol)
{
    float cosP = std::cos(limbPitch);
    float sinP = std::sin(limbPitch);
    float cosY = std::cos(bodyYaw);
    float sinY = std::sin(bodyYaw);

    // 8 vértices en espacio local
    float lx[8] = { -hx,  hx,  hx, -hx, -hx,  hx,  hx, -hx };
    float ly[8] = { -hy, -hy,  hy,  hy, -hy, -hy,  hy,  hy };
    float lz[8] = { -hz, -hz, -hz, -hz,  hz,  hz,  hz,  hz };

    Point2D sp[8];
    float depth[8];

    for (int i = 0; i < 8; i++) {
        // 1. Rotación de articulación (pitch en X local)
        float py = ly[i] * cosP - lz[i] * sinP;
        float pz = ly[i] * sinP + lz[i] * cosP;
        float px = lx[i];

        // 2. Desplazamiento al centro de la parte
        float wx0 = px + cx;
        float wy0 = py + cy;
        float wz0 = pz + cz;

        // 3. Rotación de cuerpo (yaw en Z)
        float wx = wx0 * cosY - wy0 * sinY;
        float wy = wx0 * sinY + wy0 * cosY;
        float wz = wz0;

        // 4. Proyección axonométrica de cámara ORAS (pitch 36º: cos=0.819f, sin=0.573f)
        sp[i].x = baseSX + (int)(wx * scale);
        sp[i].y = baseSY - (int)((wy * 0.573f + wz * 0.819f) * scale);
        depth[i] = wy * 0.819f - wz * 0.573f;
    }

    // Definición de las 6 caras (vértices ordenados en sentido horario)
    static const int faceIndices[6][4] = {
        { 4, 7, 6, 5 }, // Top (+Z)
        { 0, 1, 2, 3 }, // Bottom (-Z)
        { 0, 4, 5, 1 }, // Front (-Y)
        { 2, 6, 7, 3 }, // Back (+Y)
        { 1, 5, 6, 2 }, // Right (+X)
        { 3, 7, 4, 0 }  // Left (-X)
    };

    static const float faceNormals[6][3] = {
        {  0.0f,  0.0f,  1.0f },
        {  0.0f,  0.0f, -1.0f },
        {  0.0f, -1.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f }
    };

    const Color colors[6] = { topCol, botCol, frontCol, backCol, rightCol, leftCol };

    // Vector solar Hoenn (N·L)
    const float sunX = 0.35f;
    const float sunY = -0.45f;
    const float sunZ = 0.82f;

    for (int f = 0; f < 6; f++) {
        if (s_faceCount >= MAX_3D_FACES) break;

        const int *idx = faceIndices[f];
        Point2D p0 = sp[idx[0]];
        Point2D p1 = sp[idx[1]];
        Point2D p2 = sp[idx[2]];
        Point2D p3 = sp[idx[3]];

        // Back-face culling en espacio de pantalla (signed area / winding order)
        int cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (cross <= 0) continue; // Oculta por estar de espaldas a la cámara

        // Rotar normal de la cara
        float fnx = faceNormals[f][0];
        float fny = faceNormals[f][1];
        float fnz = faceNormals[f][2];

        float pny = fny * cosP - fnz * sinP;
        float pnz = fny * sinP + fnz * cosP;
        float pnx = fnx;

        float wnx = pnx * cosY - pny * sinY;
        float wny = pnx * sinY + pny * cosY;
        float wnz = pnz;

        // Producto escalar N·L con iluminación cenital solar
        float ndotl = wnx * sunX + wny * sunY + wnz * sunZ;
        float diffuse = (ndotl > 0.0f) ? ndotl : 0.0f;
        float lightIntensity = 0.48f + 0.52f * diffuse;

        const Color &base = colors[f];
        u8 r = (u8)std::min(255, (int)(base.r * lightIntensity));
        u8 g = (u8)std::min(255, (int)(base.g * lightIntensity));
        u8 b = (u8)std::min(255, (int)(base.g * lightIntensity));

        ProjectedFace &face = s_faceBuffer[s_faceCount++];
        face.depth = (depth[idx[0]] + depth[idx[1]] + depth[idx[2]] + depth[idx[3]]) * 0.25f;
        face.p[0] = p0;
        face.p[1] = p1;
        face.p[2] = p2;
        face.p[3] = p3;
        face.col = Color(r, g, b, base.a);
    }
}

// Agrega un detalle poligonal flotante orientado (ojos, cremallera, emblemas)
static void AddQuad3D(const float v[4][3], const float normal[3],
                      float limbPitch, float bodyYaw,
                      int baseSX, int baseSY, float scale,
                      const Color &col)
{
    if (s_faceCount >= MAX_3D_FACES) return;

    float cosP = std::cos(limbPitch);
    float sinP = std::sin(limbPitch);
    float cosY = std::cos(bodyYaw);
    float sinY = std::sin(bodyYaw);

    Point2D sp[4];
    float depth[4];

    for (int i = 0; i < 4; i++) {
        float py = v[i][1] * cosP - v[i][2] * sinP;
        float pz = v[i][1] * sinP + v[i][2] * cosP;
        float px = v[i][0];

        float wx = px * cosY - py * sinY;
        float wy = px * sinY + py * cosY;
        float wz = pz;

        sp[i].x = baseSX + (int)(wx * scale);
        sp[i].y = baseSY - (int)((wy * 0.573f + wz * 0.819f) * scale);
        depth[i] = wy * 0.819f - wz * 0.573f;
    }

    int cross = (sp[1].x - sp[0].x) * (sp[2].y - sp[0].y) - (sp[1].y - sp[0].y) * (sp[2].x - sp[0].x);
    if (cross <= 0) return;

    float pny = normal[1] * cosP - normal[2] * sinP;
    float pnz = normal[1] * sinP + normal[2] * cosP;
    float pnx = normal[0];

    float wnx = pnx * cosY - pny * sinY;
    float wny = pnx * sinY + pny * cosY;
    float wnz = pnz;

    float ndotl = wnx * 0.35f + wny * (-0.45f) + wnz * 0.82f;
    float diffuse = (ndotl > 0.0f) ? ndotl : 0.0f;
    float lightIntensity = 0.55f + 0.45f * diffuse;

    u8 r = (u8)std::min(255, (int)(col.r * lightIntensity));
    u8 g = (u8)std::min(255, (int)(col.g * lightIntensity));
    u8 b = (u8)std::min(255, (int)(col.b * lightIntensity));

    ProjectedFace &face = s_faceBuffer[s_faceCount++];
    face.depth = (depth[0] + depth[1] + depth[2] + depth[3]) * 0.25f - 0.25f; // Bias hacia la cámara
    face.p[0] = sp[0];
    face.p[1] = sp[1];
    face.p[2] = sp[2];
    face.p[3] = sp[3];
    face.col = Color(r, g, b, col.a);
}

// ------------------------------------------------------------
// Renderizado 3D de Bruno (Brendan) con Cinemática y Sombreado
// ------------------------------------------------------------
static void DrawBrendanAvatar3D(const Screen &screen, int sx, int sy, float scale, float yaw, float walkPhase) {
    ResetFaceBuffer();

    // Paletas oficiales Pokémon ORAS - Bruno (Brendan)
    const Color cCap       = Color(248, 250, 255); // Gorro blanco
    const Color cCapShd    = Color(210, 218, 232); // Sombras de pliegue del gorro
    const Color cGrn       = Color(32, 185, 80);   // Banda verde esmeralda
    const Color cGrnDark   = Color(20, 135, 55);   // Borde de la banda
    const Color cVisor     = Color(24, 26, 32);    // Visera negra
    const Color cHair      = Color(32, 34, 40);    // Cabello negro/antracita
    const Color cSkin      = Color(255, 224, 192); // Tono de piel anime
    const Color cSkinShd   = Color(230, 194, 164); // Sombra de piel
    const Color cRed       = Color(228, 36, 46);   // Chaqueta ciclista Rubí
    const Color cRedDark   = Color(175, 22, 32);   // Sombra de chaqueta
    const Color cGold      = Color(255, 215, 45);  // Cremallera dorada
    const Color cWhite     = Color(245, 245, 250); // Cuello blanco y detalles
    const Color cBagGrn    = Color(35, 145, 75);   // Mochila de explorador verde
    const Color cBagDark   = Color(22, 105, 50);   // Sombra de mochila
    const Color cShorts    = Color(28, 30, 38);    // Malla ciclista oscura
    const Color cStripe    = Color(45, 205, 95);   // Franja verde lateral
    const Color cSole      = Color(240, 244, 250); // Suela blanca de zapatillas
    const Color cEye       = Color(22, 40, 72);    // Ojo anime azul marino

    // Cinemática de caminata e idle
    float legAngleL = std::sin(walkPhase) * 0.45f;
    float legAngleR = -legAngleL;
    float armAngleL = -legAngleL * 0.85f;
    float armAngleR = legAngleL * 0.85f;
    float bounceZ   = std::abs(std::sin(walkPhase)) * 1.0f;
    float idleBreath = std::sin((float)(svcGetSystemTick() / 25000000ULL)) * 0.4f;
    float torsoZ    = 15.0f + bounceZ + idleBreath;

    // 1. Zapatos (Suela + Cuerpo verde)
    // Pie izquierdo
    AddBox3D(-3.5f, 0.0f, 1.0f,  2.2f, 3.2f, 1.0f, legAngleL, yaw, sx, sy, scale, cGrn, cSole, cSole, cSole, cSole, cSole);
    AddBox3D(-3.5f, 0.0f, 3.0f,  2.0f, 2.6f, 1.0f, legAngleL, yaw, sx, sy, scale, cGrn, cGrn, cGrn, cGrn, cGrnDark, cGrn);
    // Pie derecho
    AddBox3D( 3.5f, 0.0f, 1.0f,  2.2f, 3.2f, 1.0f, legAngleR, yaw, sx, sy, scale, cGrn, cSole, cSole, cSole, cSole, cSole);
    AddBox3D( 3.5f, 0.0f, 3.0f,  2.0f, 2.6f, 1.0f, legAngleR, yaw, sx, sy, scale, cGrn, cGrn, cGrn, cGrn, cGrn, cGrnDark);

    // 2. Piernas y Mallas ciclistas
    AddBox3D(-3.5f, 0.0f, 7.0f,  1.8f, 1.8f, 3.0f, legAngleL, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkin);
    AddBox3D(-3.5f, 0.0f, 11.0f, 2.0f, 2.0f, 2.0f, legAngleL, yaw, sx, sy, scale, cShorts, cShorts, cShorts, cShorts, cShorts, cStripe);
    AddBox3D( 3.5f, 0.0f, 7.0f,  1.8f, 1.8f, 3.0f, legAngleR, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkin);
    AddBox3D( 3.5f, 0.0f, 11.0f, 2.0f, 2.0f, 2.0f, legAngleR, yaw, sx, sy, scale, cShorts, cShorts, cShorts, cShorts, cStripe, cShorts);

    // 3. Pelvis y Cintura
    AddBox3D(0.0f, 0.0f, torsoZ - 1.5f, 5.2f, 2.8f, 1.5f, 0.0f, yaw, sx, sy, scale, cShorts, cShorts, cShorts, cShorts, cShorts, cShorts);

    // 4. Torso y Chaqueta Rubí
    AddBox3D(0.0f, 0.0f, torsoZ + 4.5f, 5.5f, 3.2f, 4.5f, 0.0f, yaw, sx, sy, scale, cWhite, cShorts, cRed, cRedDark, cShorts, cShorts);

    // Cremallera dorada en el frente (-Y)
    float zipQuad[4][3] = {
        { -0.5f, -3.25f, torsoZ + 1.0f },
        {  0.5f, -3.25f, torsoZ + 1.0f },
        {  0.5f, -3.25f, torsoZ + 7.5f },
        { -0.5f, -3.25f, torsoZ + 7.5f }
    };
    float normFront[3] = { 0.0f, -1.0f, 0.0f };
    AddQuad3D(zipQuad, normFront, 0.0f, yaw, sx, sy, scale, cGold);

    // 5. Mochila de Explorador en la Espalda (+Y)
    AddBox3D(0.0f, 3.2f, torsoZ + 4.0f, 4.2f, 1.8f, 4.0f, 0.0f, yaw, sx, sy, scale, cBagGrn, cBagDark, cBagDark, cBagGrn, cBagDark, cBagDark);

    // 6. Brazos articulados
    // Brazo izquierdo
    AddBox3D(-6.8f, 0.0f, torsoZ + 6.0f, 1.5f, 1.5f, 2.5f, armAngleL, yaw, sx, sy, scale, cRed, cRed, cRed, cRed, cRed, cRedDark);
    AddBox3D(-6.8f, 0.0f, torsoZ + 1.5f, 1.3f, 1.3f, 2.5f, armAngleL, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkinShd);
    // Brazo derecho
    AddBox3D( 6.8f, 0.0f, torsoZ + 6.0f, 1.5f, 1.5f, 2.5f, armAngleR, yaw, sx, sy, scale, cRed, cRed, cRed, cRed, cRedDark, cRed);
    AddBox3D( 6.8f, 0.0f, torsoZ + 1.5f, 1.3f, 1.3f, 2.5f, armAngleR, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkinShd, cSkin);

    // 7. Cabeza y Rostro
    float headZ = torsoZ + 13.5f;
    AddBox3D(0.0f, 0.0f, headZ, 5.0f, 4.5f, 4.5f, 0.0f, yaw, sx, sy, scale, cHair, cSkinShd, cSkin, cHair, cSkin, cSkin);

    // Ojos anime en la cara frontal (-Y)
    float eyeL[4][3] = {
        { -3.0f, -4.55f, headZ - 0.8f },
        { -1.4f, -4.55f, headZ - 0.8f },
        { -1.4f, -4.55f, headZ + 1.2f },
        { -3.0f, -4.55f, headZ + 1.2f }
    };
    float eyeR[4][3] = {
        {  1.4f, -4.55f, headZ - 0.8f },
        {  3.0f, -4.55f, headZ - 0.8f },
        {  3.0f, -4.55f, headZ + 1.2f },
        {  1.4f, -4.55f, headZ + 1.2f }
    };
    AddQuad3D(eyeL, normFront, 0.0f, yaw, sx, sy, scale, cEye);
    AddQuad3D(eyeR, normFront, 0.0f, yaw, sx, sy, scale, cEye);

    // Brillos especulares de los ojos
    float glintL[4][3] = {
        { -2.8f, -4.58f, headZ + 0.3f },
        { -2.0f, -4.58f, headZ + 0.3f },
        { -2.0f, -4.58f, headZ + 1.0f },
        { -2.8f, -4.58f, headZ + 1.0f }
    };
    float glintR[4][3] = {
        {  1.6f, -4.58f, headZ + 0.3f },
        {  2.4f, -4.58f, headZ + 0.3f },
        {  2.4f, -4.58f, headZ + 1.0f },
        {  1.6f, -4.58f, headZ + 1.0f }
    };
    AddQuad3D(glintL, normFront, 0.0f, yaw, sx, sy, scale, Color::White);
    AddQuad3D(glintR, normFront, 0.0f, yaw, sx, sy, scale, Color::White);

    // 8. Visera negra sobresaliente (-Y)
    AddBox3D(0.0f, -4.0f, headZ + 2.0f, 5.4f, 1.8f, 0.6f, 0.0f, yaw, sx, sy, scale, cVisor, cVisor, cVisor, cVisor, cVisor, cVisor);

    // 9. Banda Verde Esmeralda
    AddBox3D(0.0f, 0.0f, headZ + 3.0f, 5.4f, 4.9f, 1.0f, 0.0f, yaw, sx, sy, scale, cGrnDark, cGrnDark, cGrn, cGrn, cGrn, cGrn);

    // 10. Cúpula del Gorro Blanco con sombreado
    AddBox3D(0.0f, 0.5f, headZ + 6.0f, 5.2f, 5.0f, 2.5f, 0.0f, yaw, sx, sy, scale, cCap, cCapShd, cCap, cCapShd, cCap, cCap);
    AddBox3D(0.0f, 0.8f, headZ + 8.5f, 4.0f, 4.0f, 1.2f, 0.0f, yaw, sx, sy, scale, cCap, cCapShd, cCap, cCapShd, cCap, cCap);

    // Renderizar todo con ordenamiento de profundidad 3D
    RenderSortedFaces(screen);
}

// ------------------------------------------------------------
// Renderizado 3D de Aura (May) con Cinemática y Sombreado
// ------------------------------------------------------------
static void DrawMayAvatar3D(const Screen &screen, int sx, int sy, float scale, float yaw, float walkPhase) {
    ResetFaceBuffer();

    // Paletas oficiales Pokémon ORAS - Aura (May)
    const Color cHair      = Color(122, 68, 38);   // Castaño oscuro sedoso
    const Color cHairDark  = Color(85, 42, 22);    // Sombra de cabello
    const Color cHairLight = Color(165, 96, 56);   // Brillo del flequillo
    const Color cSkin      = Color(255, 224, 192); // Tono de piel anime
    const Color cSkinShd   = Color(230, 194, 164); // Sombra de piel
    const Color cBandana   = Color(225, 34, 46);   // Bandana roja Rubí
    const Color cBandDark  = Color(168, 20, 30);   // Sombra de bandana
    const Color cWhite     = Color(245, 245, 250); // Cuello y detalles blancos
    const Color cTunic     = Color(230, 36, 46);   // Túnica roja
    const Color cTunicDark = Color(170, 22, 30);   // Sombra de túnica
    const Color cShorts    = Color(32, 34, 40);    // Malla ciclista oscura
    const Color cPouch     = Color(252, 210, 38);  // Riñonera amarilla brillante
    const Color cPouchDark = Color(205, 160, 22);  // Sombra de la riñonera
    const Color cGlove     = Color(245, 245, 250); // Guantes blancos
    const Color cGloveGrn  = Color(30, 160, 70);   // Detalles verdes de guantes
    const Color cSole      = Color(240, 244, 250); // Suela zapatillas
    const Color cShoe      = Color(228, 40, 48);   // Zapatillas rojas
    const Color cEye       = Color(24, 76, 150);   // Ojos anime azul zafiro

    // Cinemática de caminata e idle
    float legAngleL = std::sin(walkPhase) * 0.45f;
    float legAngleR = -legAngleL;
    float armAngleL = -legAngleL * 0.85f;
    float armAngleR = legAngleL * 0.85f;
    float bounceZ   = std::abs(std::sin(walkPhase)) * 1.0f;
    float idleBreath = std::sin((float)(svcGetSystemTick() / 25000000ULL)) * 0.4f;
    float torsoZ    = 15.0f + bounceZ + idleBreath;

    // 1. Zapatillas deportivas rojas con suela blanca
    AddBox3D(-3.2f, 0.0f, 1.0f, 2.0f, 3.0f, 1.0f, legAngleL, yaw, sx, sy, scale, cShoe, cSole, cSole, cSole, cSole, cSole);
    AddBox3D(-3.2f, 0.0f, 3.0f, 1.8f, 2.4f, 1.0f, legAngleL, yaw, sx, sy, scale, cShoe, cShoe, cShoe, cShoe, cShoe, cShoe);
    AddBox3D( 3.2f, 0.0f, 1.0f, 2.0f, 3.0f, 1.0f, legAngleR, yaw, sx, sy, scale, cShoe, cSole, cSole, cSole, cSole, cSole);
    AddBox3D( 3.2f, 0.0f, 3.0f, 1.8f, 2.4f, 1.0f, legAngleR, yaw, sx, sy, scale, cShoe, cShoe, cShoe, cShoe, cShoe, cShoe);

    // 2. Piernas y Mallas ciclistas oscuras
    AddBox3D(-3.2f, 0.0f, 7.0f,  1.7f, 1.7f, 3.0f, legAngleL, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkin);
    AddBox3D(-3.2f, 0.0f, 11.0f, 1.9f, 1.9f, 2.0f, legAngleL, yaw, sx, sy, scale, cShorts, cShorts, cShorts, cShorts, cShorts, cShorts);
    AddBox3D( 3.2f, 0.0f, 7.0f,  1.7f, 1.7f, 3.0f, legAngleR, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkin);
    AddBox3D( 3.2f, 0.0f, 11.0f, 1.9f, 1.9f, 2.0f, legAngleR, yaw, sx, sy, scale, cShorts, cShorts, cShorts, cShorts, cShorts, cShorts);

    // 3. Pelvis / Faldita de la Túnica Roja
    AddBox3D(0.0f, 0.0f, torsoZ - 1.5f, 5.0f, 2.8f, 1.8f, 0.0f, yaw, sx, sy, scale, cTunic, cShorts, cTunic, cTunicDark, cTunic, cTunic);

    // 4. Riñonera Amarilla Icónica en el Costado Derecho (+X)
    AddBox3D(4.8f, 0.4f, torsoZ - 1.0f, 1.8f, 2.5f, 1.8f, 0.0f, yaw, sx, sy, scale, cPouch, cPouchDark, cPouch, cPouchDark, cPouch, cPouchDark);

    // 5. Torso y Túnica Roja Rubí
    AddBox3D(0.0f, 0.0f, torsoZ + 4.5f, 5.2f, 3.0f, 4.5f, 0.0f, yaw, sx, sy, scale, cWhite, cTunic, cTunic, cTunicDark, cTunic, cTunic);

    // Pechera / Cuello blanco en el frente (-Y)
    float bibQuad[4][3] = {
        { -2.0f, -3.05f, torsoZ + 6.0f },
        {  2.0f, -3.05f, torsoZ + 6.0f },
        {  1.2f, -3.05f, torsoZ + 8.8f },
        { -1.2f, -3.05f, torsoZ + 8.8f }
    };
    float normFront[3] = { 0.0f, -1.0f, 0.0f };
    AddQuad3D(bibQuad, normFront, 0.0f, yaw, sx, sy, scale, cWhite);

    // 6. Brazos y Guantes Ciclistas
    // Brazo izquierdo
    AddBox3D(-6.4f, 0.0f, torsoZ + 5.5f, 1.4f, 1.4f, 2.2f, armAngleL, yaw, sx, sy, scale, cTunic, cTunic, cTunic, cTunic, cTunic, cTunicDark);
    AddBox3D(-6.4f, 0.0f, torsoZ + 2.0f, 1.3f, 1.3f, 1.8f, armAngleL, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkin, cSkinShd);
    AddBox3D(-6.4f, 0.0f, torsoZ - 0.2f, 1.4f, 1.4f, 1.2f, armAngleL, yaw, sx, sy, scale, cGlove, cGloveGrn, cGlove, cGlove, cGlove, cGlove);
    // Brazo derecho
    AddBox3D( 6.4f, 0.0f, torsoZ + 5.5f, 1.4f, 1.4f, 2.2f, armAngleR, yaw, sx, sy, scale, cTunic, cTunic, cTunic, cTunic, cTunicDark, cTunic);
    AddBox3D( 6.4f, 0.0f, torsoZ + 2.0f, 1.3f, 1.3f, 1.8f, armAngleR, yaw, sx, sy, scale, cSkin, cSkin, cSkin, cSkin, cSkinShd, cSkin);
    AddBox3D( 6.4f, 0.0f, torsoZ - 0.2f, 1.4f, 1.4f, 1.2f, armAngleR, yaw, sx, sy, scale, cGlove, cGloveGrn, cGlove, cGlove, cGlove, cGlove);

    // 7. Cabeza y Rostro Femenino
    float headZ = torsoZ + 13.0f;
    AddBox3D(0.0f, 0.0f, headZ, 4.8f, 4.4f, 4.4f, 0.0f, yaw, sx, sy, scale, cHair, cSkinShd, cSkin, cHair, cSkin, cSkin);

    // Cabello cayendo hacia la espalda (+Y)
    AddBox3D(0.0f, 3.6f, headZ - 2.5f, 4.6f, 1.6f, 4.0f, 0.0f, yaw, sx, sy, scale, cHair, cHairDark, cHairDark, cHair, cHairDark, cHairDark);

    // Ojos anime azul zafiro en la cara frontal (-Y)
    float eyeL[4][3] = {
        { -2.8f, -4.45f, headZ - 0.9f },
        { -1.2f, -4.45f, headZ - 0.9f },
        { -1.2f, -4.45f, headZ + 1.3f },
        { -2.8f, -4.45f, headZ + 1.3f }
    };
    float eyeR[4][3] = {
        {  1.2f, -4.45f, headZ - 0.9f },
        {  2.8f, -4.45f, headZ - 0.9f },
        {  2.8f, -4.45f, headZ + 1.3f },
        {  1.2f, -4.45f, headZ + 1.3f }
    };
    AddQuad3D(eyeL, normFront, 0.0f, yaw, sx, sy, scale, cEye);
    AddQuad3D(eyeR, normFront, 0.0f, yaw, sx, sy, scale, cEye);

    // Brillos especulares de los ojos
    float glintL[4][3] = {
        { -2.6f, -4.48f, headZ + 0.3f },
        { -1.8f, -4.48f, headZ + 0.3f },
        { -1.8f, -4.48f, headZ + 1.1f },
        { -2.6f, -4.48f, headZ + 1.1f }
    };
    float glintR[4][3] = {
        {  1.4f, -4.48f, headZ + 0.3f },
        {  2.2f, -4.48f, headZ + 0.3f },
        {  2.2f, -4.48f, headZ + 1.1f },
        {  1.4f, -4.48f, headZ + 1.1f }
    };
    AddQuad3D(glintL, normFront, 0.0f, yaw, sx, sy, scale, Color::White);
    AddQuad3D(glintR, normFront, 0.0f, yaw, sx, sy, scale, Color::White);

    // Mechones de flequillo lateral
    AddBox3D(-4.2f, -2.5f, headZ - 1.0f, 1.2f, 1.8f, 3.0f, 0.0f, yaw, sx, sy, scale, cHairLight, cHairDark, cHair, cHairDark, cHairDark, cHair);
    AddBox3D( 4.2f, -2.5f, headZ - 1.0f, 1.2f, 1.8f, 3.0f, 0.0f, yaw, sx, sy, scale, cHairLight, cHairDark, cHair, cHairDark, cHair, cHairDark);

    // 8. Bandana Roja envolvente
    AddBox3D(0.0f, 0.0f, headZ + 3.2f, 5.2f, 4.8f, 1.6f, 0.0f, yaw, sx, sy, scale, cBandana, cBandDark, cBandana, cBandDark, cBandana, cBandana);

    // 9. Lazo Icónico / Orejas de Conejo de Aura en la Bandana (Arriba-Centro)
    AddBox3D( 0.0f, 0.2f, headZ + 5.2f, 2.0f, 1.6f, 1.0f, 0.0f, yaw, sx, sy, scale, cBandana, cBandDark, cBandana, cBandana, cBandana, cBandana);
    // Oreja izquierda del lazo inclinada
    AddBox3D(-2.8f, 0.2f, headZ + 6.8f, 1.6f, 1.0f, 1.8f, 0.25f, yaw, sx, sy, scale, cBandana, cBandDark, cBandana, cBandDark, cBandana, cBandDark);
    // Oreja derecha del lazo inclinada
    AddBox3D( 2.8f, 0.2f, headZ + 6.8f, 1.6f, 1.0f, 1.8f, -0.25f, yaw, sx, sy, scale, cBandana, cBandDark, cBandana, cBandDark, cBandDark, cBandana);

    // Renderizar todo con ordenamiento de profundidad 3D
    RenderSortedFaces(screen);
}

// ------------------------------------------------------------
// Renderizado en el Mundo (Overworld)
// ------------------------------------------------------------
static void DrawWorldAvatar(const Screen &screen, const PlayerPacket &rp, const std::string &name, float dx, float dy, float dz, float dist, float yaw, float walkPhase) {
    if (!screen.IsTop) return;
    if (dist > 450.0f) return; // Radio amplio de visibilidad (hasta 450m)

    // Proyección isométrica espacial calibrada (Sincronización vertical 1:1 v12.6)
    int sx = 200 + (int)(dx * g_projScaleX);
    int sy = 130 + (int)(dy * g_projScaleY - dz * g_projScaleZ);

    // Indicador direccional en el borde si el jugador está fuera del campo visual de la pantalla
    if (sx < -20 || sx > 420 || sy < -20 || sy > 260) {
        int edge_x = std::max(30, std::min(370, sx));
        int edge_y = std::max(26, std::min(214, sy));

        int nlen = (int)name.length();
        int bW = std::max(46, nlen * 6 + 14);
        screen.DrawRect(edge_x - (bW / 2), edge_y - 9, bW, 17, Color(10, 16, 26, 215), true);
        screen.DrawRect(edge_x - (bW / 2), edge_y - 9, bW, 17, Color(0, 220, 255, 200), false);
        char dBuf[36];
        snprintf(dBuf, sizeof(dBuf), "%s %.0fm", name.c_str(), dist);
        screen.Draw(dBuf, edge_x - (bW / 2) + 3, edge_y - 6, Color::Yellow, Color(0,0,0,0));
        return;
    }

    // Escala de perspectiva con distancia (cerca ~1.0, lejos ~0.65)
    float scale = std::max(0.65f, std::min(1.15f, 1.0f - (dist - 4.0f) * 0.005f));

    // Posición directa de pantalla: sin fuerzas artificiales ni efecto imán
    int draw_sx = sx;
    int draw_sy = sy;

    // 1. SOMBRA SUAVE MULTICAPA EN EL SUELO (3 niveles elípticos translúcidos)
    screen.DrawRect(draw_sx - (int)(12 * scale), draw_sy - (int)(2 * scale), std::max(1, (int)(24 * scale)), std::max(1, (int)(5 * scale)), Color(10, 14, 22, 90), true);
    screen.DrawRect(draw_sx - (int)(8 * scale),  draw_sy - (int)(2 * scale), std::max(1, (int)(16 * scale)), std::max(1, (int)(4 * scale)), Color(10, 14, 22, 140), true);
    screen.DrawRect(draw_sx - (int)(4 * scale),  draw_sy - (int)(1 * scale), std::max(1, (int)(8 * scale)),  std::max(1, (int)(3 * scale)), Color(8,  10, 16, 200), true);

    // 2. RENDERIZADO DEL MODELO 3D (BRUNO O AURA)
    bool isMay = (rp.model_id == 171); // 171 = Aura (May), 172 = Bruno (Brendan)
    if (!isMay) {
        DrawBrendanAvatar3D(screen, draw_sx, draw_sy, scale, yaw, walkPhase);
    } else {
        DrawMayAvatar3D(screen, draw_sx, draw_sy, scale, yaw, walkPhase);
    }

    // 3. PLACA DE NOMBRE HOLOGRÁFICA FLOTANTE
    int nlen = (int)name.length();
    int badgeW = std::max(38, nlen * 6 + 10);
    int badgeX = draw_sx - (badgeW / 2);
    int badgeY = draw_sy - (int)(46 * scale);

    screen.DrawRect(badgeX, badgeY, badgeW, 11, Color(8, 14, 24, 150), true);
    screen.DrawRect(badgeX, badgeY, badgeW, 11, Color(0, 220, 255, 180), false);
    screen.Draw(name, badgeX + 4, badgeY + 1, Color::Yellow, Color(0,0,0,0));

    // Distancia en metros encima de la placa
    char distStr[16];
    snprintf(distStr, sizeof(distStr), "%.1fm", dist);
    int dX = draw_sx - 10;
    screen.Draw(distStr, dX, badgeY - 10, (dist < 5.0f) ? Color::Lime : Color(0, 220, 255), Color(0,0,0,0));

    // 4. BOCADILLO DE DIÁLOGO / EMOTE FLOTANTE
    int slot = rp.player_id - 1;
    if (slot >= 0 && slot < MAX_MMO_PLAYERS) {
        if (!g_playerChatMsg[slot].empty() && (osGetTime() - g_playerChatTime[slot] < 5000)) {
            const std::string &bubbleTxt = g_playerChatMsg[slot];
            int bW = (int)bubbleTxt.length() * 6 + 10;
            int bX = draw_sx - (bW / 2);
            int bY = badgeY - 26;

            screen.DrawRect(bX - 2, bY - 2, bW + 4, 15, Color(245, 245, 255, 230), true);
            screen.DrawRect(bX - 2, bY - 2, bW + 4, 15, Color::Black, false);
            screen.DrawRect(draw_sx - 1, bY + 13, 3, 3, Color(245, 245, 255, 230), true);
            screen.Draw(bubbleTxt, bX + 3, bY + 1, Color::Black, Color(0,0,0,0));
        }
    }
}

// ============================================================
// Localizador en Mapa de Hoenn (AreaNav en Pantalla Inferior 320x240)
//
// LOGICA DE 3 NIVELES:
//   1. map_id == 7260 (exterior Hoenn): proyeccion geometrica lineal calibrada.
//      Guarda la posicion calculada en el cache por jugador.
//   2. map_id en HOENN_LOCATIONS (interior conocido): usa el pixel fijo de la tabla.
//   3. Cualquier otro interior desconocido: usa el ULTIMO EXTERIOR CONOCIDO del cache.
//      Esto es critico porque los interiores tienen coordenadas locales (ej. x=15, y=8)
//      que proyectadas como coordenadas globales darian posiciones completamente erroneas.
//
// CALIBRACION DEL EXTERIOR (2 puntos confirmados en vivo):
//   Rustboro City:  world(2079, 1143) -> AreaNav pixel(41, 104)
//   Fallarbor Town: world(3357, 1863) -> AreaNav pixel(71, 46)
//   scaleX = (71-41)/(3357-2079) = 0.023474 px/unidad mundo
//   scaleY = (46-104)/(1863-1143) = -0.080556 px/unidad mundo
// ============================================================
static void ResolveHoennLocation(u8 player_id, u16 map_id, float x, float y,
                                  int &pinX, int &pinY, std::string &locName) {
    // Cache de ultima posicion EXTERIOR conocida por jugador (indices 0..MAX_MMO_PLAYERS-1)
    static int         s_cacheX[MAX_MMO_PLAYERS]    = {130,130,130,130,130,130};
    static int         s_cacheY[MAX_MMO_PLAYERS]    = {108,108,108,108,108,108};
    static std::string s_cacheName[MAX_MMO_PLAYERS] = {"Mauville City","Mauville City","Mauville City","Mauville City","Mauville City","Mauville City"};

    int pidx = (player_id >= 1 && player_id <= MAX_MMO_PLAYERS) ? (player_id - 1) : 0;

    // ── NIVEL 1: Interior conocido en la tabla (Cuevas, Gimnasios, Mazmorras) ──────
    // Comprobar SIEMPRE antes que el exterior para evitar que las coordenadas locales
    // de cuevas/interiores contaminen el cache exterior.
    for (const auto& loc : HOENN_LOCATIONS) {
        if (loc.map_id == map_id) {
            locName = loc.name;
            pinX    = loc.px;
            pinY    = loc.py;
            // Si es un exterior de ciudad (map_id >= 200), registrar en cache exterior
            if (map_id >= 200) {
                s_cacheX[pidx]    = pinX;
                s_cacheY[pidx]    = pinY;
                s_cacheName[pidx] = locName;
            }
            // Si es cueva o interior (map_id < 200), NO sobreescribir el cache exterior
            return;
        }
    }

    // ── NIVEL 2: Caso especial Centro Pokémon (map_id == 2) ────────────────────────
    if (map_id == 2) {
        pinX    = s_cacheX[pidx];
        pinY    = s_cacheY[pidx];
        locName = s_cacheName[pidx] + " (Centro Pkmn)";
        return;
    }

    // ── NIVEL 3: Overworld exterior de Hoenn (map_id == 7260 o 0) ─────────────────
    // Sistema de proyección geodésica de Hoenn ORAS calibrado milimétricamente:
    // Fallarbor Town (world: 3357, 1863) -> AreaNav pixel (71, 46)
    // Rustboro City  (world: 2079, 1143) -> AreaNav pixel (41, 104)
    if (map_id == 7260 || map_id == 0) {
        if (x != 0.0f && y != 0.0f) {
            const float scaleX = 0.023474f;
            const float scaleY = 0.080556f;
            int rawX = 71 + (int)((x - 3357.0f) * scaleX);
            int rawY = 46 - (int)((y - 1863.0f) * scaleY);

            pinX = std::max(36, std::min(244, rawX));
            pinY = std::max(32, std::min(175, rawY));

            // Identificar ciudad/ruta más cercana en píxeles
            int bestDist2 = 999999;
            const char* bestName = "Hoenn";
            for (const auto& loc : HOENN_LOCATIONS) {
                if (loc.map_id < 200 && loc.map_id > 0) continue; // Excluir interiores
                int dX = pinX - loc.px;
                int dY = pinY - loc.py;
                int d2 = dX * dX + dY * dY;
                if (d2 < bestDist2) { bestDist2 = d2; bestName = loc.name; }
            }
            locName = bestName;

            s_cacheX[pidx]    = pinX;
            s_cacheY[pidx]    = pinY;
            s_cacheName[pidx] = bestName;
        } else {
            pinX    = s_cacheX[pidx];
            pinY    = s_cacheY[pidx];
            locName = s_cacheName[pidx];
        }
        return;
    }

    // ── NIVEL 4: Interior desconocido -> mantener cache exterior intacto ──────────
    pinX    = s_cacheX[pidx];
    pinY    = s_cacheY[pidx];
    locName = s_cacheName[pidx];
}


static void DrawBottomScreenMapRadar(const Screen &screen) {
    if (screen.IsTop) return;
    if (g_currentMapId == 2) return; // En interiores del Centro Pokemon no interferir con la pantalla tactil

    // Verificacion de AreaNav activo (evitar dibujar si esta abierto PlayNav PSS o DexNav)
    u8* pHeader = screen.GetFramebuffer(36, 14);
    if (pHeader) {
        u8 hr = (screen.Format == GSP_RGBA8_OES) ? pHeader[0] : pHeader[2];
        u8 hg = pHeader[1];
        u8 hb = (screen.Format == GSP_RGBA8_OES) ? pHeader[2] : pHeader[0];
        // En PlayNav (PSS), el banner superior es morado intenso
        if (hr > 90 && hb > 100 && hg < 90) return;
        // En DexNav, el banner superior es rojo/naranja
        if (hr > 150 && hg < 110 && hb < 90) return;
    }

    struct FriendSummary {
        std::string name;
        std::string location;
        float dist;
    };
    FriendSummary activeFriends[MAX_MMO_PLAYERS];
    int activeCount = 0;

    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        if (!g_remotePlayerActive[i] || i == (g_playerId - 1)) continue;
        const PlayerPacket& rp = g_latestRemotePackets[i];

        float dx = rp.x - g_localX;
        float dy = rp.y - g_localY;
        float dist = std::sqrt(dx*dx + dy*dy);

        int pinX = 130;
        int pinY = 108;
        std::string locName = "Hoenn";
        ResolveHoennLocation(rp.player_id, (u16)rp.map_id, rp.x, rp.y, pinX, pinY, locName);

        // Delimitar al marco visible del mapa en AreaNav
        pinX = std::max(36, std::min(244, pinX));
        pinY = std::max(32, std::min(175, pinY));

        std::string fname = GetPlayerDisplayName(rp.player_id);

        if (activeCount < MAX_MMO_PLAYERS) {
            activeFriends[activeCount] = { fname, locName, dist };
            activeCount++;
        }

        // 1. PIN RADAR HOLOGRAFICO EN EL MAPA (Rombo brillante)
        screen.DrawRect(pinX - 2, pinY - 2, 5, 5, Color(0, 230, 255, 240), true);
        screen.DrawRect(pinX - 3, pinY - 3, 7, 7, Color(0, 100, 220, 180), false);
        screen.DrawRect(pinX - 1, pinY - 1, 3, 3, Color::White, true);

        // 2. ETIQUETA FLOTANTE CON NOMBRE (Anti-colision vertical escalonada)
        int nlen = (int)fname.length();
        int tW = nlen * 6 + 8;
        int tX = pinX - (tW / 2);
        int tY = (activeCount % 2 == 1) ? (pinY - 13) : (pinY + 5);

        if (tX < 28) tX = 28;
        if (tX + tW > 256) tX = 256 - tW;
        if (tY < 28) tY = 28;
        if (tY > 165) tY = 165;

        screen.DrawRect(tX - 1, tY - 1, tW + 2, 11, Color(8, 14, 24, 210), true);
        screen.DrawRect(tX - 1, tY - 1, tW + 2, 11, Color(0, 220, 255, 190), false);
        screen.Draw(fname, tX + 3, tY, Color::Yellow, Color(0, 0, 0, 0));
    }


    // 3. BARRA INFORMATIVA INFERIOR (1 SOLA LINEA ROTATIVA COMPACTA EN EL AGUA)
    // Se dibuja en Y = 173 (borde inferior interno del mapa, NO tapa los botones tactiles de Y >= 188)
    if (activeCount > 0) {
        static u64 s_tickerTime = 0;
        static int s_tickerIdx = 0;
        u64 now = osGetTime();
        if (now - s_tickerTime > 3000) {
            s_tickerIdx = (s_tickerIdx + 1) % activeCount;
            s_tickerTime = now;
        }
        if (s_tickerIdx >= activeCount) s_tickerIdx = 0;

        const auto& curF = activeFriends[s_tickerIdx];
        char tickerBuf[80];
        if (curF.dist < 450.0f) {
            snprintf(tickerBuf, sizeof(tickerBuf), "[%d/%d] %s: %s (%.0fm)",
                     s_tickerIdx + 1, activeCount, curF.name.c_str(), curF.location.c_str(), curF.dist);
        } else {
            snprintf(tickerBuf, sizeof(tickerBuf), "[%d/%d] %s: %s (%.0fm)",
                     s_tickerIdx + 1, activeCount, curF.name.c_str(), curF.location.c_str(), curF.dist);
        }

        screen.DrawRect(28, 173, 228, 11, Color(10, 16, 26, 215), true);
        screen.DrawRect(28, 173, 228, 11, Color(0, 190, 240, 170), false);
        screen.Draw(tickerBuf, 32, 174, Color(0, 255, 180), Color(0, 0, 0, 0));
    }
}

// ============================================================
// Callback HUD: Radar OSD Reubicado y Auto-Ocultado en Menús (v12.6)
// ============================================================
static bool s_isMenuOpen = false;

bool OSD_Callback(const Screen &screen) {
    // 0. GUARDIA TOTAL: DETECCIÓN DE TRANSICIONES DE ESCENA, PUERTAS Y COMBATES
    // Durante puertas (0x08803C20 == 0x5544) o combates (0x081FB478 == 0x40001),
    // NUNCA dibujar OSD ni acceder al framebuffer para no interrumpir los scripts del juego
    if (IsSceneTransitionActive()) {
        return true;
    }

    if (!screen.IsTop) {
        s_isMenuOpen = false;

        // Detección positiva de botón CANCELAR / SALIR en subpantalla de menús (Bolsa, Equipo, PC)
        u8* pCancelBtn = screen.GetFramebuffer(280, 220);
        if (pCancelBtn) {
            u8 cr = (screen.Format == GSP_RGBA8_OES) ? pCancelBtn[0] : pCancelBtn[2];
            u8 cg = pCancelBtn[1];
            u8 cb = (screen.Format == GSP_RGBA8_OES) ? pCancelBtn[2] : pCancelBtn[0];
            if (cr > 175 && cg < 130 && cb < 120) {
                s_isMenuOpen = true;
            }
        }

        // En la pantalla inferior, verificar si PokéNav Plus está activo en el Overworld
        u8* pNav = screen.GetFramebuffer(122, 14);
        u8* pDex = screen.GetFramebuffer(36, 14);
        bool navActive = false;
        if (pNav && pDex) {
            u8 dr = (screen.Format == GSP_RGBA8_OES) ? pDex[0] : pDex[2];
            u8 dg = pDex[1];
            u8 db = (screen.Format == GSP_RGBA8_OES) ? pDex[2] : pDex[0];
            u8 ar = (screen.Format == GSP_RGBA8_OES) ? pNav[0] : pNav[2];
            u8 ag = pNav[1];
            u8 ab = (screen.Format == GSP_RGBA8_OES) ? pNav[2] : pNav[0];
            // En PokéNav Plus, las pestañas superiores tienen los colores de las 4 apps (DexNav rojo, AreaNav azul, etc.)
            if ((dr > 90 && dg < 110) || (ab > 80 && ag > 60) || (db > 90 && dr > 70)) {
                navActive = true;
            }
        }
        // También verificar el icono azul inferior derecho de standby de PokéNav
        u8* pStandby = screen.GetFramebuffer(300, 220);
        if (pStandby) {
            u8 sb_r = (screen.Format == GSP_RGBA8_OES) ? pStandby[0] : pStandby[2];
            u8 sb_b = (screen.Format == GSP_RGBA8_OES) ? pStandby[2] : pStandby[0];
            if (sb_b > 110 && sb_r < 90) navActive = true;
        }

        s_isBottomNavActive = navActive;

        // Renderizar Localizador en Mapa de Hoenn en la Pantalla Inferior
        // (Solo si el HUD está activo, no estamos en interior del Centro Pokémon y PokéNav está activo)
        if (g_showHUD && g_currentMapId != 2 && s_isBottomNavActive) {
            DrawBottomScreenMapRadar(screen);
        }
        return true;
    }

    // --- PANTALLA SUPERIOR (400x240) ---
    // Ocultar si se detectó positivamente menú 2D en subpantalla (Bolsa, Equipo, PC)
    if (s_isMenuOpen) {
        return true;
    }

    // Detección directa en pantalla superior de menús secundarios (Bolsa, Equipo Pokémon, PC de Centro Pokémon)
    // En la Bolsa, la franja de descripción de objeto cubre Y=210..225 con marco oscuro
    u8* pBagCheck = screen.GetFramebuffer(200, 215);
    if (pBagCheck) {
        u8 br = (screen.Format == GSP_RGBA8_OES) ? pBagCheck[0] : pBagCheck[2];
        u8 bg = pBagCheck[1];
        u8 bb = (screen.Format == GSP_RGBA8_OES) ? pBagCheck[2] : pBagCheck[0];
        // En la bolsa / PC el fondo inferior es un panel gris-azul sólido uniforme
        if (br < 45 && bg < 45 && bb < 55 && std::abs((int)br - (int)bg) < 6) {
            u8* pBagCheck2 = screen.GetFramebuffer(204, 215);
            if (pBagCheck2) {
                u8 b2r = (screen.Format == GSP_RGBA8_OES) ? pBagCheck2[0] : pBagCheck2[2];
                u8 b2g = pBagCheck2[1];
                u8 b2b = (screen.Format == GSP_RGBA8_OES) ? pBagCheck2[2] : pBagCheck2[0];
                if (br == b2r && bg == b2g && bb == b2b) {
                    return true; // Menú 2D de Bolsa o PC activo
                }
            }
        }
    }

    // En el Menú de Equipo Pokémon (Party), el slot 1 tiene una barra verde brillante de PS en (110, 45)
    u8* pPartyHp = screen.GetFramebuffer(110, 45);
    if (pPartyHp) {
        u8 hr = (screen.Format == GSP_RGBA8_OES) ? pPartyHp[0] : pPartyHp[2];
        u8 hg = pPartyHp[1];
        u8 hb = (screen.Format == GSP_RGBA8_OES) ? pPartyHp[2] : pPartyHp[0];
        if (hg > 180 && hr < 130 && hb < 130) {
            return true; // Menú de Equipo Pokémon activo
        }
    }

    if (local_player_x != 0 && Process::CheckAddress(local_player_x, MEMPERM_READ)) {
        Process::ReadFloat(local_player_x, g_localX);
        if (local_player_y != 0 && Process::CheckAddress(local_player_y, MEMPERM_READ))
            Process::ReadFloat(local_player_y, g_localY);
        if (local_player_z != 0 && Process::CheckAddress(local_player_z, MEMPERM_READ))
            Process::ReadFloat(local_player_z, g_localZ);
    }

    bool conn = g_client.is_connected();

    // 1. HUD ELEGANTE CON COORDENADAS EN VIVO Y ESTADO DE AMIGOS
    if (g_showHUD) {
        screen.DrawRect(3, 3, 215, 50, Color(8, 14, 24, 165), true);
        screen.DrawRect(3, 3, 215, 50, conn ? Color(0, 220, 120, 180) : Color(220, 40, 40, 180), false);
        screen.DrawRect(3, 3, 3, 50, conn ? Color::Lime : Color::Red, true);

        char hLine1[48];
        snprintf(hLine1, sizeof(hLine1), "MMO [%s] %s", g_roomCode.c_str(), g_playerName.c_str());
        screen.Draw(hLine1, 9, 5, conn ? Color::Lime : Color::Red, Color(0, 0, 0, 0));

        char hLine2[64];
        if (local_player_x != 0 || g_localX != 0.0f) {
            snprintf(hLine2, sizeof(hLine2), "Pos: X:%.1f  Y:%.1f  (M:%d)", g_localX, g_localY, g_currentMapId);
            screen.Draw(hLine2, 9, 16, Color(255, 225, 60), Color(0, 0, 0, 0));
        } else {
            screen.Draw("Coords: SELECT -> Paso 1 y 2", 9, 16, Color(255, 170, 40), Color(0, 0, 0, 0));
        }

        // Línea 3: Información y distancia del amigo más cercano en tiempo real
        int activeFriendsCount = 0;
        float minFriendDist = 999999.0f;
        std::string nearestFriendName = "";
        std::string nearestFriendLoc  = "Hoenn";
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            if (g_remotePlayerActive[i] && i != (g_playerId - 1)) {
                activeFriendsCount++;
                float dx = g_latestRemotePackets[i].x - g_localX;
                float dy = g_latestRemotePackets[i].y - g_localY;
                float d = std::sqrt(dx*dx + dy*dy);
                if (d < minFriendDist) {
                    minFriendDist = d;
                    nearestFriendName = GetPlayerDisplayName(i + 1);
                    int px = 0, py = 0;
                    ResolveHoennLocation(g_latestRemotePackets[i].player_id, (u16)g_latestRemotePackets[i].map_id, g_latestRemotePackets[i].x, g_latestRemotePackets[i].y, px, py, nearestFriendLoc);
                }
            }
        }

        char hLine3[64];
        if (activeFriendsCount > 0) {
            if (minFriendDist < 250.0f) {
                snprintf(hLine3, sizeof(hLine3), "%s: %.0fm (%s) [Cerca]", nearestFriendName.c_str(), minFriendDist, nearestFriendLoc.c_str());
                screen.Draw(hLine3, 9, 27, Color::Lime, Color(0, 0, 0, 0));
            } else {
                snprintf(hLine3, sizeof(hLine3), "%s: %.0fm (%s)", nearestFriendName.c_str(), minFriendDist, nearestFriendLoc.c_str());
                screen.Draw(hLine3, 9, 27, Color(0, 220, 255), Color(0, 0, 0, 0));
            }
        } else {
            screen.Draw(conn ? "Esperando jugadores en sala..." : "Sin conexion", 9, 27, Color(160, 160, 160), Color(0, 0, 0, 0));
        }

        // Linea 4 DEBUG: coordenadas crudas del amigo mas cercano (para calibracion de mapa)
        if (activeFriendsCount > 0) {
            int dbgIdx = -1;
            for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                if (g_remotePlayerActive[i] && i != (g_playerId - 1)) { dbgIdx = i; break; }
            }
            if (dbgIdx >= 0) {
                char dbgLine[64];
                snprintf(dbgLine, sizeof(dbgLine), "DBG M:%u X:%.0f Y:%.0f",
                    (unsigned)g_latestRemotePackets[dbgIdx].map_id,
                    g_latestRemotePackets[dbgIdx].x,
                    g_latestRemotePackets[dbgIdx].y);
                screen.Draw(dbgLine, 9, 38, Color(255, 80, 80), Color(0, 0, 0, 0));
            }
        }
    }

    // 2. RADAR REUBICADO A ZONA DESPEJADA (Oculto también si g_showHUD es falso)
    u32 rx_center = 24;
    u32 ry_center = 216;
    const u32 r_half = 15;

    if (g_radarPos == 0) {
        // Esquina Inferior-Izquierda (Totalmente libre de Pokémon, barras de vida y menús)
        rx_center = 24;
        ry_center = 216;
    } else if (g_radarPos == 1) {
        // Esquina Superior-Derecha
        rx_center = 378;
        ry_center = 20;
    }

    if (g_showHUD && g_radarPos != 2) { // 2 = Oculto (o cuando se oculta el HUD completo)
        screen.DrawRect(rx_center - r_half, ry_center - r_half, 30, 30, Color(8, 14, 24, 110), true);
        screen.DrawRect(rx_center - r_half - 1, ry_center - r_half - 1, 32, 32, Color(0, 190, 255, 140), false);

        // Miras sutiles del radar
        screen.DrawRect(rx_center - r_half + 3, ry_center, (r_half * 2) - 6, 1, Color(30, 60, 90, 140), true);
        screen.DrawRect(rx_center, ry_center - r_half + 3, 1, (r_half * 2) - 6, Color(30, 60, 90, 140), true);
        screen.Draw("N", rx_center - 2, ry_center - r_half + 1, Color(255, 220, 50, 200), Color(0, 0, 0, 0));

        // Jugador local en el centro del radar
        screen.DrawRect(rx_center - 1, ry_center - 1, 3, 3, Color(0, 255, 128, 220), true);
    }

    // Coordenadas suavizadas con LERP (60 FPS) para erradicar tirones y parpadeos
    static float s_renderX[MAX_MMO_PLAYERS] = {0};
    static float s_renderY[MAX_MMO_PLAYERS] = {0};
    static float s_renderZ[MAX_MMO_PLAYERS] = {0};
    static float s_renderYaw[MAX_MMO_PLAYERS] = {0};
    static float s_walkPhase[MAX_MMO_PLAYERS] = {0};

    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        if (!g_remotePlayerActive[i] || i == (g_playerId - 1)) continue;
        const PlayerPacket& rp = g_latestRemotePackets[i];

        float diffX = rp.x - s_renderX[i];
        float diffY = rp.y - s_renderY[i];
        float diffZ = rp.z - s_renderZ[i];
        float diffDist = std::sqrt(diffX * diffX + diffY * diffY);

        if (s_renderX[i] == 0.0f || diffDist > 60.0f) {
            s_renderX[i] = rp.x;
            s_renderY[i] = rp.y;
            s_renderZ[i] = rp.z;
            s_walkPhase[i] = 0.0f;
        } else if (diffDist < 0.40f) {
            // Zona de reposo / micro-ajuste: fijar suavemente y orientar con dirección cardinal de red
            s_renderX[i] = rp.x;
            s_renderY[i] = rp.y;
            s_renderZ[i] = rp.z;
            s_walkPhase[i] *= 0.82f; // Desvanecer animación de caminata hacia reposo idle

            // Orientación estable hacia la dirección enviada en el paquete (0=Sur, 1=Norte, 2=Oeste, 3=Este)
            float targetYaw = 0.0f;
            u8 f = rp.facing & 0x03;
            if (f == 0) targetYaw = 0.0f;          // Sur
            else if (f == 1) targetYaw = 3.14159f; // Norte
            else if (f == 2) targetYaw = -1.5708f; // Oeste
            else if (f == 3) targetYaw = 1.5708f;  // Este

            float aDiff = targetYaw - s_renderYaw[i];
            while (aDiff > 3.14159f) aDiff -= 6.28318f;
            while (aDiff < -3.14159f) aDiff += 6.28318f;
            s_renderYaw[i] += aDiff * 0.25f;
        } else {
            // Movimiento suave, fluido y sincronizado (0.20f) que sigue el camino fielmente sin cortar esquinas
            float lerpSpeed = 0.20f;
            s_renderX[i] += diffX * lerpSpeed;
            s_renderY[i] += diffY * lerpSpeed;
            s_renderZ[i] += diffZ * lerpSpeed;

            // Orientación continua 3D hacia el vector de avance (Sincronizada con eje Y v12.6)
            float targetYaw = std::atan2(diffX, diffY);
            float aDiff = targetYaw - s_renderYaw[i];
            while (aDiff > 3.14159f) aDiff -= 6.28318f;
            while (aDiff < -3.14159f) aDiff += 6.28318f;
            s_renderYaw[i] += aDiff * 0.25f;

            // Avance rítmico de pasos de caminata
            s_walkPhase[i] += diffDist * 0.35f;
        }

        float dx = s_renderX[i] - g_localX;
        float dy = s_renderY[i] - g_localY;
        float dz = s_renderZ[i] - g_localZ;
        float dist = std::sqrt(dx*dx + dy*dy);

        std::string fname = GetPlayerDisplayName(rp.player_id);

        // Renderizar avatar 3D si están en el mismo mapa o distancia visual (< 450m)
        bool sameMap = (rp.map_id == g_currentMapId) || (dist < 450.0f);
        if (sameMap && dist < 450.0f && (local_player_x != 0 || g_localX != 0.0f)) {
            // Renderizar Avatar 3D Auténtico de Hoenn en el mundo de juego
            DrawWorldAvatar(screen, rp, fname, dx, dy, dz, dist, s_renderYaw[i], s_walkPhase[i]);

            // Radar con sensibilidad suave calibrada (0.08f) y eje Y vertical sincronizado
            if (g_showHUD && g_radarPos != 2) {
                int dot_x = rx_center + (int)(dx * 0.08f);
                int dot_y = ry_center + (int)(dy * 0.08f);

                if (dot_x < (int)(rx_center - r_half + 2)) dot_x = rx_center - r_half + 2;
                if (dot_x > (int)(rx_center + r_half - 2)) dot_x = rx_center + r_half - 2;
                if (dot_y < (int)(ry_center - r_half + 2)) dot_y = ry_center - r_half + 2;
                if (dot_y > (int)(ry_center + r_half - 2)) dot_y = ry_center + r_half - 2;

                screen.DrawRect(dot_x - 1, dot_y - 1, 3, 3, Color(0, 230, 255, 230), true);

                if (dist < 8.0f) {
                    screen.DrawRect(dot_x - 2, dot_y - 2, 5, 5, Color(255, 220, 0, 240), false);
                }
            }
        }
    }

    if (!g_chatOverlay.empty()) {
        if (osGetTime() - g_chatTime < 5000) {
            screen.DrawRect(4, 218, 392, 18, Color(12, 18, 28, 180), true);
            screen.DrawRect(4, 218, 392, 18, Color(255, 200, 0, 200), false);
            screen.Draw(g_chatOverlay, 8, 222, Color::Yellow, Color(0, 0, 0, 0));
        } else {
            g_chatOverlay = "";
        }
    }
    return true;
}

// Inyección continua en cada frame del motor de juego (30/60 FPS)
void OnFrameCallback(Time time) {
    if (IsSceneTransitionActive()) return;

    // Lectura continua de coordenadas en vivo con filtro de seguridad anti-corrupción
    // (100% libre de sobreescritura de posición: ambos jugadores pueden caminar juntos sin rebotar)
    if (local_player_x != 0 && Process::CheckAddress(local_player_x, MEMPERM_READ)) {
        float tx = 0, tz = 0, ty = 0;
        if (Process::ReadFloat(local_player_x, tx) &&
            Process::ReadFloat(local_player_z, tz) &&
            Process::ReadFloat(local_player_y, ty))
        {
            if (std::isfinite(tx) && std::isfinite(tz) && std::isfinite(ty) &&
                std::abs(tx) < 50000.0f && std::abs(tz) < 50000.0f && std::abs(ty) < 50000.0f)
            {
                g_localX = tx;
                g_localZ = tz;
                g_localY = ty;
            } else if (std::abs(tx) >= 50000.0f || std::abs(ty) >= 50000.0f) {
                // Filtro de seguridad: rechazar coordenadas anómalas tras combates
                local_player_x = 0;
                local_player_base = 0;
                TryAutoLinkBackground();
            }
        }
    }
}

// ============================================================
// Menus y Configuracion
// ============================================================
static void MenuSetName(MenuEntry *e) {
    Keyboard kb("Escribe tu Nombre de Usuario:");
    kb.SetMaxLength(12);
    std::string input;
    if (kb.Open(input) == 0 && !input.empty()) {
        g_playerName = input;
        File f("mmo_name.txt", File::RWC | File::TRUNCATE);
        if (f.IsOpen()) f.Write(input.c_str(), input.length());
        g_osdMsg = "MMO OK | Sala:" + g_roomCode + " [" + g_playerName + "]";
        OSD::Notify("Nombre guardado: " + input, Color::Lime);
    }
}

static void MenuSetIP(MenuEntry *e) {
    Keyboard kb("IP del Servidor (ZeroTier):");
    kb.SetMaxLength(64);
    std::string input;
    if (kb.Open(input) == 0 && !input.empty()) {
        g_serverIP = input;
        File f("mmo_ip.txt", File::RWC | File::TRUNCATE);
        if (f.IsOpen()) f.Write(input.c_str(), input.length());
        if (g_client.is_connected()) g_client.disconnect();
        OSD::Notify("IP guardada: " + input);
    }
}

static void MenuSetRoom(MenuEntry *e) {
    Keyboard kb("Codigo de Sala (4 letras o numeros):");
    kb.SetMaxLength(8);
    std::string input;
    if (kb.Open(input) == 0 && !input.empty()) {
        g_roomCode = input;
        if (g_client.is_connected()) g_client.disconnect();
        OSD::Notify("Sala configurada: " + input);
    }
}

static void MenuStatus(MenuEntry *e) {
    std::string s;
    if (g_client.is_connected()) {
        char tmp[320];
        snprintf(tmp, sizeof(tmp), 
            "ESTADO: Conectado OK\n"
            "Tu Nombre: %s\n"
            "Tu ID de Jugador: %s\n"
            "Sala: %s\n"
            "IP Servidor: %s\n"
            "Mapa Actual: %d\n"
            "NPCs 3D vinculados: %d\n"
            "Evento 1-a-1: %s",
            g_playerName.c_str(), g_playerIdStr.c_str(), g_roomCode.c_str(), g_serverIP.c_str(),
            g_currentMapId, g_entityManager.get_slot_count(),
            g_eventActive ? ("Activo con " + g_activeEventPartner).c_str() : "Ninguno");
        s = tmp;
    } else {
        s = "ESTADO: Desconectado\nTu Nombre: " + g_playerName + "\nIP: " + g_serverIP + "\nSala: " + g_roomCode;
    }
    MessageBox("Estado de Conexion MMO", s)();
}

static void MenuReconnect(MenuEntry *e) {
    g_client.disconnect();
    OSD::Notify("Reconectando al servidor...");
}

// ============================================================
// Plataforma de Combate e Intercambio 1-a-1 (Transporte a PSS)
// ============================================================
static void MenuSendEventRequest(MenuEntry *e) {
    if (!g_client.is_connected()) { OSD::Notify("No conectado al servidor."); return; }

    std::vector<std::string> friend_names;
    std::vector<int> friend_pids;

    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        if (i == (g_playerId - 1)) continue;
        if (g_remotePlayerActive[i]) {
            friend_names.push_back(GetPlayerDisplayName(i + 1));
            friend_pids.push_back(i + 1);
        }
    }

    if (friend_pids.empty()) {
        MessageBox("Sin Amigos", "No hay amigos conectados en tu sala para invitar.")();
        return;
    }

    Keyboard kbFriend("Selecciona a que amigo invitar:");
    kbFriend.Populate(friend_names);
    int choice = kbFriend.Open();
    if (choice < 0 || choice >= (int)friend_pids.size()) return;

    int target_pid = friend_pids[choice];
    std::string target_name = friend_names[choice];

    std::vector<std::string> event_types = { "1. Desafiar a Combate Pokemon (PSS Original)", "2. Solicitar Intercambio Pokemon (PSS Original)" };
    Keyboard kbType("Tipo de Evento:");
    kbType.Populate(event_types);
    int tchoice = kbType.Open();
    if (tchoice < 0) return;

    uint8_t ev_type = (tchoice == 0) ? 1 : 2;
    g_activeEventType = ev_type;
    g_incomingSender = target_pid;

    QueueCustomPacket(MMO_PKT_EVENT_REQ, (uint8_t)target_pid, ev_type);

    const char* tstr = (ev_type == 1) ? "Combate" : "Intercambio";
    char msg[320];
    snprintf(msg, sizeof(msg),
        "Has enviado la invitacion de %s a %s.\n\n"
        "En cuanto tu amigo acepte, abran la pantalla tactil del PSS en Pokemon ORAS para conectar y jugar el encuentro original.",
        tstr, target_name.c_str());
    MessageBox("Invitacion Enviada", msg)();
    OSD::Notify("Invitacion enviada a " + target_name, Color::Lime);
}

// Sincronizacion PSS (v10.9: 100% libre de inyecciones en RAM)
// La comunicacion local inalambrica es gestionada nativamente por la sala de Azahar
static void InjectPSSPasserby(const std::string &friend_name, uint8_t friend_pid) {
    // Zero-RAM-Writes: la sala de Azahar enlaza el PSS nativamente sin corromper la memoria
    return;
}

// Guia de Sincronizacion PSS (Passersby / Friends)
static void MenuActivatePSS(MenuEntry *e) {
    MessageBox("Como activar el PSS en Azahar (PC y Android)",
        "Para que tu amigo aparezca en 'Friends' o 'Passersby' del PSS oficial:\n\n"
        "1. SALA MULTIJUGADOR EN AZAHAR:\n"
        "   - En PC: Ve al menu superior de Azahar -> Multijugador -> Crear Sala (o Unirse a Sala).\n"
        "   - En Android: Menu de Azahar -> Multijugador -> Unirse a Sala e ingresa la IP del host.\n"
        "   - Ambos deben estar en la misma sala para que Azahar transmita las senales inalambricas del 3DS.\n\n"
        "2. CONEXION INALAMBRICA DEL PSS:\n"
        "   - En la pantalla inferior (PSS), presiona el boton circular azul de Wi-Fi arriba a la derecha.\n"
        "   - Toca 'Si' cuando pregunte si deseas conectarte a la comunicacion local.\n\n"
        "3. ¡APARICION INMEDIATA!:\n"
        "   - Tu amigo aparecera en la seccion 'Passersby' (Transeuntes).\n"
        "   - Tocas su icono y eliges 'Combate' o 'Intercambio' con todas las funciones oficiales del juego.")();
}

static void MenuRespondEvent(MenuEntry *e) {
    if (!g_hasIncomingEvent) {
        MessageBox("Sin Solicitudes", "No tienes ninguna invitacion pendiente.")();
        return;
    }

    const char* tstr = (g_incomingType == 1) ? "COMBATE POKEMON" : "INTERCAMBIO POKEMON";
    char msg[320];
    snprintf(msg, sizeof(msg), 
        "¡%s te ha invitado a un %s!\n\n"
        "Al aceptar, ambos jugadores quedaran sincronizados para iniciar el encuentro oficial.\n\n"
        "¿Deseas ACEPTAR la invitacion?",
        g_incomingName.c_str(), tstr);

    MessageBox mb("Invitacion de " + std::string(tstr), msg, DialogType::DialogYesNo);
    if (mb()) { // Si (Aceptar)
        QueueCustomPacket(MMO_PKT_EVENT_RESP, g_incomingSender, 1);
        InjectPSSPasserby(g_incomingName, g_incomingSender);
        g_eventActive = true;
        g_activeEventPartner = g_incomingName;
        g_activeEventType = g_incomingType;
        g_hasIncomingEvent = false;

        MessageBox("Invitacion Aceptada",
            "✓ ¡Invitacion aceptada!\n\n"
            "Ahora abran el PSS en la pantalla tactil de Pokemon ORAS, toquen el boton Wi-Fi y seleccionen a su amigo en Passersby.")();
        OSD::Notify("✓ ¡Aceptado! Abre el PSS en la pantalla tactil.", Color::Lime);
    } else { // No (Rechazar)
        QueueCustomPacket(MMO_PKT_EVENT_RESP, g_incomingSender, 2);
        g_hasIncomingEvent = false;
        OSD::Notify("Invitacion rechazada.", Color::Red);
    }
}

static void MenuEndEvent(MenuEntry *e) {
    if (!g_eventActive) {
        MessageBox("Aviso", "No tienes ninguna invitacion de evento activa.")();
        return;
    }

    QueueCustomPacket(MMO_PKT_EVENT_END, 0, 0);
    g_eventActive = false;
    std::string partner = g_activeEventPartner;
    g_activeEventPartner = "";
    OSD::Notify("Invitacion finalizada con " + partner, Color::Orange);
    MessageBox("Evento Finalizado", "Has cerrado la invitacion con " + partner + ".")();
}

// ============================================================
// Ajuste de Escala del Motor de Avatares 3D (WSEP 3.5 Masterpiece)
// ============================================================
static void MenuToggleScale(MenuEntry *e) {
    g_scaleMode = (g_scaleMode + 1) % 3;
    if (g_scaleMode == 0) {
        g_projScaleX = 1.25f;
        g_projScaleY = 0.95f;
        g_projScaleZ = 0.75f;
        MessageBox("Escala 3D Calibrada (v12.6)", "Modo: Calibrado 1:1 Oficial (1.25x0.95)\nAnclaje perfecto al suelo sin efecto iman.")();
        OSD::Notify("Escala 3D: Calibrado 1:1 Oficial (1.25x0.95)", Color::Lime);
    } else if (g_scaleMode == 1) {
        g_projScaleX = 1.80f;
        g_projScaleY = 1.40f;
        g_projScaleZ = 1.10f;
        MessageBox("Escala 3D Calibrada (v12.6)", "Modo: Zoom Medio (1.80x1.40)\nIdeal para interiores y vista cercana.")();
        OSD::Notify("Escala 3D: Zoom Medio (1.80x1.40)", Color::Lime);
    } else {
        g_projScaleX = 0.90f;
        g_projScaleY = 0.70f;
        g_projScaleZ = 0.55f;
        MessageBox("Escala 3D Calibrada (v12.6)", "Modo: Panoramico Amplio (0.90x0.70)\nIdeal para rutas abiertas y vistas lejanas.")();
        OSD::Notify("Escala 3D: Panoramico Amplio (0.90x0.70)", Color::Lime);
    }
}

// ============================================================
// Seleccion de Personaje (Aura / Bruno)
// ============================================================
static void MenuToggleCharacter(MenuEntry *e) {
    if (local_model_id == 171) {
        local_model_id = 172; // Bruno (Brendan)
        OSD::Notify("Personaje MMO: Bruno (Brendan)", Color::Lime);
    } else {
        local_model_id = 171; // Aura (May)
        OSD::Notify("Personaje MMO: Aura (May)", Color(255, 105, 180));
    }
}


// ============================================================
// Ajuste de HUD y Radar
// ============================================================
static void MenuToggleHUD(MenuEntry *e) {
    g_showHUD = !g_showHUD;
    if (g_showHUD) {
        OSD::Notify("HUD y Radar: Visibles", Color::Lime);
    } else {
        OSD::Notify("HUD y Radar: Ocultos (Pantalla Limpia)", Color::Orange);
    }
}

static void MenuToggleRadarPos(MenuEntry *e) {
    g_radarPos = (g_radarPos + 1) % 3;
    if (g_radarPos == 0) {
        OSD::Notify("Radar: Esquina Inferior-Izquierda (Recomendado)", Color::Lime);
    } else if (g_radarPos == 1) {
        OSD::Notify("Radar: Esquina Superior-Derecha", Color::Cyan);
    } else {
        OSD::Notify("Radar: Oculto", Color::Orange);
    }
}

// Chat Rapido y Personalizado
static void MenuChat(MenuEntry *e) {
    if (!g_client.is_connected()) { OSD::Notify("No conectado."); return; }
    std::vector<std::string> ch;
    ch.push_back(">> Escribir Mensaje Personalizado <<");
    for (auto& m : CHAT_MSGS) ch.push_back(m.texto);
    Keyboard kb("Enviar Mensaje:"); kb.Populate(ch);
    int idx = kb.Open();
    if (idx == 0) {
        Keyboard customKb("Escribe tu mensaje:");
        customKb.SetMaxLength(19);
        std::string customMsg;
        if (customKb.Open(customMsg) == 0 && !customMsg.empty()) {
            QueueCustomPacket(MMO_PKT_CHAT, 255, 0, customMsg.c_str());
            g_chatOverlay = "[" + g_playerName + "]: " + customMsg;
            g_chatTime = osGetTime();
            OSD::Notify("Mensaje enviado: " + customMsg, Color::Yellow);
        }
    } else if (idx > 0 && idx <= (int)(sizeof(CHAT_MSGS)/sizeof(CHAT_MSGS[0]))) {
        int chatIdx = idx - 1;
        QueueCustomPacket(MMO_PKT_CHAT, CHAT_MSGS[chatIdx].id, 0);
        g_chatOverlay = "[" + g_playerName + "]: " + std::string(CHAT_MSGS[chatIdx].texto);
        g_chatTime = osGetTime();
        OSD::Notify("Mensaje enviado");
    }
}

// Emotes
static void MenuEmote(MenuEntry *e) {
    if (!g_client.is_connected()) { OSD::Notify("No conectado."); return; }
    std::vector<std::string> em = { "Saludar", "Saltar", "Corazon", "Interrogacion", "Exclamacion" };
    Keyboard kb("Seleccionar Emote:"); kb.Populate(em);
    int idx = kb.Open();
    if (idx >= 0 && idx < (int)em.size()) {
        QueueCustomPacket(MMO_PKT_EMOTE, (uint8_t)(idx + 1), 0);
        g_chatOverlay = "[" + g_playerName + "]: " + em[idx];
        g_chatTime = osGetTime();
        OSD::Notify("Emote: " + em[idx]);
    }
}

static void MenuHelp(MenuEntry *e) {
    MessageBox("Guia Rapida ORAS MMO v12.6 (Sincronizacion Fiel)",
        "1. MOVIMIENTO VERTICAL Y RADAR SINCRONIZADO (v12.6):\n"
        "   - Inversion corregida: Al moverte hacia arriba o abajo tu amigo y tu se ven y siguen perfectamente en pantalla y radar.\n"
        "   - El HUD y radar cargan de inmediato para todos los jugadores en el Overworld.\n\n"
        "2. PANTALLA LIMPIA (AUTO-OCULTADO Y MANUAL):\n"
        "   - Se oculta de forma automatica y limpia en Combates, Bolsa de Objetos y Menu de Equipo Pokemon.\n"
        "   - En el menu SELECT, 'Mostrar / Ocultar HUD y Radar' permite apagarlo o encenderlo manualmente.\n\n"
        "3. CHAT PERSONALIZADO Y SOCIAL:\n"
        "   - En Chat y Emotes -> 'Enviar Chat', elige '>> Escribir Mensaje Personalizado <<' para escribir libremente con el teclado.\n\n"
        "4. CALIBRACION FIABLE (PASO 1 Y PASO 2):\n"
        "   - Usa el Paso 1 y Paso 2 para fijar coordenadas con maxima estabilidad.\n\n"
        "5. COMBATES E INTERCAMBIOS (PSS ORIGINAL):\n"
        "   - Unanse a la misma sala multijugador en Azahar y toquen el icono Wi-Fi del PSS.\n\n"
        "6. ESTABILIDAD TOTAL Y CERO BUGS:\n"
        "   - Entradas y salidas a Centros Pokemon e interiores 100% fluidas y protegidas con la guardia nativa de puertas.")();
}

// ============================================================
// Punto de entrada del Plugin CTRPF
// ============================================================
namespace CTRPluginFramework {
    int main(void) {
        FwkSettings::Get().CloseMenuWithB = true;

        PluginMenu *menu = new PluginMenu("Pokemon ORAS MMO Sync v12.6 (Sincronizacion Fiel)", 12, 6, 0);
        menu->SynchronizeWithFrame(true);

        // 1. Menu de Calibracion y Ajustes Visuales
        MenuFolder *syncF = new MenuFolder("Calibracion y Ajustes Visuales");
        *syncF += new MenuEntry("1. [PASO 1] Registrar Posicion Inicial", nullptr, MenuLinkPlayer1);
        *syncF += new MenuEntry("2. [PASO 2] Confirmar Movimiento (Caminar 3 pasos)", nullptr, MenuLinkPlayer2);
        *syncF += new MenuEntry("Cambiar Personaje (Aura / Bruno)", nullptr, MenuToggleCharacter);
        *syncF += new MenuEntry("Mostrar / Ocultar HUD y Radar", nullptr, MenuToggleHUD);
        *syncF += new MenuEntry("Cambiar Posicion del Radar (Abajo-Izq / Arriba-Der / Oculto)", nullptr, MenuToggleRadarPos);
        *syncF += new MenuEntry("Ajustar Zoom / Escala de Avatares 3D", nullptr, MenuToggleScale);
        *menu += syncF;

        // 2. Invitaciones de Combate e Intercambio (PSS Original)
        MenuFolder *eventF = new MenuFolder("Invitaciones PSS Original (Combate e Intercambio)");
        *eventF += new MenuEntry("1. Enviar Solicitud a un Amigo", nullptr, MenuSendEventRequest);
        *eventF += new MenuEntry("2. Responder Solicitud Pendiente", nullptr, MenuRespondEvent);
        *eventF += new MenuEntry("3. Cancelar Solicitud Activa", nullptr, MenuEndEvent);
        *eventF += new MenuEntry("4. Guia: Como conectar el PSS (Friends / Passersby)", nullptr, MenuActivatePSS);
        *menu += eventF;

        // 3. Menu de Chat y Emotes
        MenuFolder *socialF = new MenuFolder("Chat y Emotes");
        *socialF += new MenuEntry("Enviar Chat Rapido", nullptr, MenuChat);
        *socialF += new MenuEntry("Enviar Emote / Saludo", nullptr, MenuEmote);
        *menu += socialF;

        // 4. Menu de Configuracion de Red y Perfil
        MenuFolder *cfgF = new MenuFolder("Configuracion de Red y Perfil");
        *cfgF += new MenuEntry("Configurar mi Nombre de Usuario", nullptr, MenuSetName);
        *cfgF += new MenuEntry("Cambiar IP del Servidor", nullptr, MenuSetIP);
        *cfgF += new MenuEntry("Cambiar Codigo de Sala", nullptr, MenuSetRoom);
        *cfgF += new MenuEntry("Ver Estado de Conexion", nullptr, MenuStatus);
        *cfgF += new MenuEntry("Forzar Reconexion", nullptr, MenuReconnect);
        *menu += cfgF;

        *menu += new MenuEntry("Ayuda / Como Jugar", nullptr, MenuHelp);

        // Inicializar OSD y bucle por frame
        OSD::Run(OSD_Callback);
        menu->OnNewFrame = OnFrameCallback;

        // Iniciar Hilo de red TCP (prioridad 0x30 en App Core, joinable)
        Thread net_thread = threadCreate(NetworkThread, NULL, 0x8000, 0x30, -2, false);

        menu->Run();

        // Limpieza al salir
        g_isRunning = false;
        threadJoin(net_thread, U64_MAX);
        threadFree(net_thread);

        g_playerMutex.Lock();
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            g_entityManager.clear_player(i);
        }
        g_playerMutex.Unlock();

        delete menu;
        return 0;
    }
}
