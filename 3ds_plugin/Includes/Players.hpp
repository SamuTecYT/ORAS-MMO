/*
 * Players.hpp — ORAS MMO v13
 *
 * Tabla de jugadores remotos (hasta 12) e interpolación por tiempo.
 *
 * Por qué así: v12.6 movía el avatar con un "lerp" del 20 % por frame hacia el último paquete.
 * Eso dependía de los FPS, cortaba esquinas (el amigo "atravesaba" vallas) y, cuando llegaban
 * varios paquetes juntos, el avatar cruzaba la pantalla a toda velocidad.
 * Ahora cada paquete trae el reloj del emisor; guardamos un historial y dibujamos al jugador
 * INTERP_DELAY_MS en el pasado, interpolando entre dos posiciones reales consecutivas. El avatar
 * recorre exactamente el mismo camino y a la misma velocidad que el jugador real.
 */
#pragma once
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include "Protocol.hpp"
#include "NetSmooth.hpp"

namespace MMO {

static constexpr int   SNAP_COUNT       = 32;
// v16: el colchón (antes fijo, 130 ms) y la predicción salen de NetSmooth.hpp, por jugador según su jitter
static constexpr int   PLAYER_TIMEOUT_MS = 15000;
static constexpr float TELEPORT_DIST    = 90.0f; // > 5 casillas entre muestras = salto (puerta, vuelo)

struct Snapshot {
    u32   t;          // reloj del emisor (ms)
    float x, y, z;
    float yaw;        // radianes
    u16   zone, area;
    u8    state;      // MoveState
    u8    flags;
};

// v14: progreso (ranking) y combate en curso de cada jugador
struct RemoteStats { bool valid; u16 hours; u8 minutes, badges; u16 caught, seen; u32 money; u8 party, avgLv, maxLv; u16 lead; u8 okMask; };
// v16: ranking guardado (lo envía el servidor y se actualiza con las STATS en vivo). Clave: nombre + modelo.
static constexpr int RANKBOOK_MAX = 64;   // = RANK_MAX_ENTRIES del servidor
struct RankBookEntry { bool used; char name[14]; u16 model; RemoteStats stats; u16 daysAgo; bool offline; u64 updated; };

struct RemoteBattle { u8 kind; u16 mySp; u8 myLv; u16 myHp, myHpMax; u16 opSp; u8 opLv; u16 opHp, opHpMax; };

// Resultado de muestrear a un jugador en el instante de render
struct RemoteView {
    bool  valid;
    u8    id;
    char  name[14];
    u16   model;
    u8    variant;     // color de ropa 0..15
    u8    acc;         // v16.5: gorro / lazo (0 como la ropa, 1 original, 2..16 color; SkinColors.hpp)
    u8    tag;         // color de etiqueta 0..15 (0 = automático)
    RemoteStats  stats;
    RemoteBattle battle;
    u64   battleAge;   // ms desde la última info de combate
    float x, y, z, yaw;
    float speed;       // unidades/s (para elegir animación)
    u16   zone, area;
    u8    state, flags;
    float animPhase;   // avance acumulado por distancia recorrida
    char  bubble[24];  // texto del bocadillo (vacío si no hay)
    u64   bubbleAge;   // ms desde que se recibió
};

struct RemotePlayer {
    bool     active = false;
    u8       id = 0;
    char     name[14] = {0};
    u16      model = MMO_MODEL_BRENDAN;
    u8       variant = 0;
    u8       acc = 0;
    u8       tag = 0;
    u8       version = 0;
    Snapshot snaps[SNAP_COUNT];
    int      count = 0, head = 0;      // head = índice del más nuevo
    s64      clockOffset = 0;          // local - emisor (filtrado por mínimo)
    s64      offQ8 = 0;                // el mismo desfase en punto fijo (x256)
    bool     offsetValid = false;
    u64      lastRecv = 0;
    u64      newestRecv = 0;               // hora local de llegada de la posición más nueva
    double   playT = 0.0;                  // reloj de reproducción (en tiempo del emisor)
    bool     playInit = false;
    u64      lastSample = 0;
    char     bubble[24] = {0};
    u64      bubbleTime = 0;
    // estado de animación (lo actualiza el render)
    float    animPhase = 0.f;
    RemoteStats  stats = {};
    RemoteBattle battle = {};
    u64          battleTime = 0;
    float    lastX = 0.f, lastY = 0.f;
    bool     lastValid = false;
    // v16: jitter de su conexión y posición mostrada (corrige errores suavemente)
    int      jitterQ4 = 0;                 // ms x16
    float    delayHold = 0.f;              // v16.1: colchón en uso (sube enseguida, baja despacio)
    double   skew = 1.0;                   // v16.1: ritmo de su reloj frente al mío (emulador al 85 % -> 0,85)
    u32      skewStamp = 0;                // punto de referencia para medirlo (su hora / la mía)
    u64      skewNow = 0;
    bool     skewRef = false;
    u64      prevRecv = 0;
    u32      prevStamp = 0;
    bool     prevValid = false;
    float    dispX = 0.f, dispY = 0.f, dispZ = 0.f;
    bool     dispValid = false;
    u16      dispZone = 0;
    u64      dispT = 0;
};

class PlayerTable {
public:
    void Clear();
    void OnPacket(const PlayerPacket &p, u64 nowMs);     // hilo de red
    void OnLeave(u8 id);
    void SetBubble(u8 id, const char *txt, u64 nowMs);
    void ExpireStale(u64 nowMs);
    int  ActiveCount();
    bool GetName(u8 id, char *out, size_t cap);
    // Rellena 'out' (MAX_MMO_PLAYERS entradas) con la vista interpolada para 'nowMs'. Devuelve cuántas son válidas.
    int  Sample(u64 nowMs, RemoteView *out);
    // v16: copia del ranking guardado (máx 'cap' entradas). Devuelve cuántas.
    int  CopyRankBook(RankBookEntry *out, int cap);
    // v16.3: borra del ranking guardado la entrada de ese nombre (al cambiarte el nombre: tu nombre viejo)
    void BookForget(const char *name);
    // v16: "J2 jit=12 ret=120 ..." por jugador activo (para el log)
    void NetSummary(char *out, size_t cap);

    u8   localId = 0;

private:
    static void Interpolate(const RemotePlayer &rp, s64 senderT, RemoteView &v);
    static void DecodeStats(const PlayerPacket &p, RemoteStats &st);
    void BookPut(const char *name, u16 model, const RemoteStats &st, u16 daysAgo, bool offline, u64 nowMs);
    void BookForgetLocked(const char *name);
    RankBookEntry m_book[RANKBOOK_MAX];
    CTRPluginFramework::Mutex m_mutex;
    RemotePlayer m_players[MAX_MMO_PLAYERS];
};

extern PlayerTable g_players;
bool SameRankName(const char *a, const char *b);   // v16: misma normalización que el servidor

} // namespace MMO
