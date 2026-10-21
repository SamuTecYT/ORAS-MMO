#include "GameState.hpp"
#include "Protocol.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>

using namespace CTRPluginFramework;

namespace Game {

// Cadena canónica (notas de investigación §2.1, verificada en vivo): GS=*(0x62F7C4), GM=*(GS+0x1C), P=GM+0x124
static const u32 GS_PTR_ADDR    = 0x0062F7C4;
static const u32 GS_EVM         = 0x18;          // GameEventManager*; evento activo = *(EVM)
static const u32 GS_GM          = 0x1C;          // GameManager*
static const u32 GS_FM          = 0x48;          // Fieldmap*, 0 fuera del mapa (combate, carga)
static const u32 GM_PLAYER      = 0x124;         // PlayerWork+4 (zona, orientación, posición)
// vtables de eventos en code.bin (fijas; KB §8.2, §10.1 R4/R5)
static const u32 VT_BATTLE_CALL = 0x005DED9C;    // EventBattleCall (padre de todo el combate)
static const u32 VT_BATTLE_DLL  = 0x005DECB4;    // EventBattleDll
static const u32 VT_MAPCHANGE   = 0x005DF0FC;    // EventMapChangeCore
static const u32 VT_APPCALL     = 0x005DEC34;    // EventAppCall: Mochila, Pokédex, Equipo... (en vivo 2026-10-19)
static const u32 EV_MAX_DEPTH   = 8;
// Respaldo si la cadena no se puede leer: bandera de combate fija (heap, 0x40001 en combate)
static const u32 BATTLE_ADDR    = 0x081FB478;
static const u32 SCAN_LO        = 0x08000000;
static const u32 SCAN_HI        = 0x08E00000;   // por encima no hay datos del juego (y leer de más es peligroso)
static const u32 SCAN_STEP      = 0x200000;     // 2 MB por tick: el escaneo completo tarda ~0,25 s sin dar tirones
static const float ASPECT_TOP   = 400.0f / 240.0f;
static const u32 ASPECT_BITS    = 0x3FD55555;   // 1.6666666f

static Mutex      s_mutex;
static LocalState s_state = {};
static u32        s_P = 0;           // dirección del bloque del jugador
static u32        s_cam = 0;         // dirección de la matriz view-projection
static u32        s_pCursor = SCAN_LO, s_cCursor = SCAN_LO;
static int        s_pMisses = 0;     // ticks seguidos con el bloque inválido
static u64        s_camBadSince = 0;
static u64        s_pRestAt = 0, s_cRestAt = 0;   // pausa tras un barrido completo sin éxito

static float s_px = 0, s_py = 0, s_speed = 0;
static u64   s_pt = 0;
static bool  s_pvalid = false;

// ---------------------------------------------------------------------------------------------
// Lectura segura: comprueba la página una vez y lee directo (mismo espacio de direcciones).
static inline bool PageOk(u32 a) { return Process::CheckAddress(a & ~0xFFFu, MEMPERM_READ); }
static inline bool RdU32(u32 a, u32 &v) { if ((a & 3) || !PageOk(a)) return false; v = *(volatile u32 *)a; return true; }
static inline bool Plausible(float v) { return std::isfinite(v) && std::fabs(v) < 60000.f; }

// Comprobación de contenido (sin llamadas al sistema): el llamador garantiza que las páginas son legibles.
static inline bool PlayerSigRaw(u32 P) {
    u16 zone = *(volatile u16 *)P;
    if (zone == 0 || zone >= 600) return false;
    if (*(volatile u16 *)(P - 0xE2) != zone) return false;          // copia "Situation" con la misma zona
    if (*(volatile u8 *)(P + 2) > 7) return false;                   // orientación 0..7
    for (int i = 0; i < 3; i++) {
        float a = *(volatile float *)(P + 0x10 + 4 * i);
        float b = *(volatile float *)(P - 0xE4 + 0x10 + 4 * i);
        if (!Plausible(a) || a != b) return false;
    }
    return true;
}

// Firma ESTRICTA (para encontrarlo): zona y posición repetidas en la copia "Situation" (P-0xE4).
static bool LooksLikePlayerBlock(u32 P) {
    if ((P & 3) || P < SCAN_LO + 0x100 || P + 0x20 > SCAN_HI) return false;
    if (!PageOk(P) || !PageOk(P - 0xE4) || !PageOk(P + 0x1C)) return false;
    return PlayerSigRaw(P);
}

// Validación LIGERA (para seguirlo cada tick): durante puertas/cargas la copia puede ir desfasada un instante.
static bool StillPlayerBlock(u32 P) {
    if (!P || (P & 3) || !PageOk(P) || !PageOk(P - 0xE4) || !PageOk(P + 0x1C)) return false;
    u16 zone = *(volatile u16 *)P;
    if (zone == 0 || zone >= 600 || *(volatile u8 *)(P + 2) > 7) return false;
    for (int i = 0; i < 3; i++) if (!Plausible(*(volatile float *)(P + 0x10 + 4 * i))) return false;
    return true;
}

static inline bool InHeap(u32 a) { return a >= SCAN_LO && a < 0x0E000000; }
static inline bool RdPtr(u32 a, u32 &v) { return RdU32(a, v) && InHeap(v); }

// Bloque del jugador por la cadena del juego (lo normal). La cadena es la fuente buena, así que basta
// la validación ligera; 0 si aún no existe (arranque, pantalla de título). chainOk = punteros legibles.
static u32 FindPlayerChain(bool &chainOk) {
    u32 gs = 0, gm = 0;
    chainOk = RdPtr(GS_PTR_ADDR, gs) && RdPtr(gs + GS_GM, gm);
    if (!chainOk) return 0;
    u32 P = gm + GM_PLAYER;
    return StillPlayerBlock(P) ? P : 0;
}

// v16: forma del jugador = FieldPlayer+0x24 (FM+0x164 -> +0x24; Native_PlayerGetForm 0x27053434 [S]).
// 0 a pie, 1 y 2 = las dos bicis (Native_PlayerSetFormToNormalForce: 1/2 -> EventPlayerCycleOff [S]),
// 3 = patines (EventPlayerRollerOff). Otros valores (surf, buceo) no son bici. 0xFF si no se puede leer.
static u8 ReadForm() {
    u32 gs = 0, fm = 0, fp = 0, w = 0;
    if (!RdPtr(GS_PTR_ADDR, gs) || !RdU32(gs + GS_FM, fm) || !InHeap(fm) || !RdPtr(fm + 0x164, fp) || !RdU32(fp + 0x24, w)) return 0xFF;
    return (u8)w;
}

// Estado del juego leído del evento activo (KB §8.2). ok=false si la cadena no se puede leer.
struct EvInfo { bool ok; u32 ev; u32 fm; bool battle; bool mapChange; bool app; bool shortcut; bool xmenu; };

// Se recorre toda la cadena ev -> ev+4 (padre): en combate el evento de arriba es EventProcessCall o
// EventBattleDll y EventBattleCall es un antepasado. EventProcessCall por sí solo NO cuenta como combate
// (es genérico y puede abrir menús); en combate real siempre cuelga de BattleDll/BattleCall (R4).
static EvInfo ReadEvents() {
    EvInfo r = {};
    u32 gs = 0, evm = 0, ev = 0, fm = 0;
    if (!RdPtr(GS_PTR_ADDR, gs) || !RdPtr(gs + GS_EVM, evm) || !RdU32(evm, ev) || !RdU32(gs + GS_FM, fm)) return r;
    r.ok = true;
    r.ev = ev;
    r.fm = fm;
    // v16.1: ¿el evento de arriba es el menú Y? (solo ese: cualquier otro sigue contando como bloqueado)
    u32 lit = 0, topVt = 0;
    if (ev && InHeap(ev) && RdU32(ev, topVt) && RdU32(DLLFIELD_LIT_ADDR, lit) && lit != DLLFIELD_UNLOADED && lit > DLLFIELD_LIT_OFF)
        r.shortcut = topVt == lit - DLLFIELD_LIT_OFF + VT_SHORTCUT_OFF;
    r.xmenu = topVt == VT_FIELDMENU;                    // v16.5: el menú X, solo si es el evento de arriba
    for (u32 i = 0; i < EV_MAX_DEPTH && ev; i++) {
        u32 vt = 0, parent = 0;
        if (!InHeap(ev) || !RdU32(ev, vt) || !RdU32(ev + 4, parent)) break;
        if (vt == VT_BATTLE_CALL || vt == VT_BATTLE_DLL) r.battle = true;
        else if (vt == VT_MAPCHANGE) r.mapChange = true;
        else if (vt == VT_APPCALL) r.app = true;
        ev = parent;
    }
    return r;
}

// Combate / carga / bloqueado / libre. sit = palabra P-0xE4 (0 si no se pudo leer).
static void ApplyState(LocalState &st, u32 P, bool haveP) {
    u16 sit = 0;
    st.shortcutMenu = false;
    st.fieldMenu = false;
    st.inApp = false;
    if (haveP && P && !(P & 3) && PageOk(P - 0xE4)) sit = *(volatile u16 *)(P - 0xE4);
    EvInfo e = ReadEvents();
    if (!e.ok) {                                        // respaldo: método antiguo
        u32 bat = 0;
        st.inBattle = RdU32(BATTLE_ADDR, bat) && bat == 0x40001;
        if (haveP) st.playerState = (sit == 2) ? STATE_FREE : (sit == 3) ? STATE_LOADING : (sit & 0xC000) ? STATE_LOCKED : STATE_UNKNOWN;
        return;
    }
    st.inBattle = e.battle;
    if (!haveP) return;
    if (e.battle)                     st.playerState = STATE_LOCKED;
    else if (e.mapChange)             st.playerState = STATE_LOADING;
    // v16.5: sin mapa por una aplicación abierta desde el menú (EventAppCall: Mochila, Pokédex, Equipo...; KB §13.9,
    // el juego quita el mapa y lo rehace al volver). Pantalla limpia, pero sigues en tu sitio para tus amigos. Sin
    // EventAppCall (título, huevos, concursos...) sigue siendo una carga normal.
    else if (!e.fm)                 { st.playerState = STATE_LOADING; st.inApp = e.app; }
    else if (e.ev && e.shortcut)    { st.playerState = STATE_FREE; st.shortcutMenu = true; }   // menú Y: tus amigos siguen
    else if (e.ev && e.xmenu)       { st.playerState = STATE_FREE; st.fieldMenu = true; }      // menú X: también
    else if (e.ev)                    st.playerState = STATE_LOCKED;      // puerta, escena, diálogo...
    // ev==0 => libre (verificado caminando). Menús/diálogos sin probar: si la palabra antigua dice
    // "bloqueado" o "cargando", se respeta para no dibujar encima de un menú.
    else if (sit == 3)                st.playerState = STATE_LOADING;
    else if (sit & 0xC000)            st.playerState = STATE_LOCKED;
    else                              st.playerState = STATE_FREE;
}

// Un tramo del escaneo por firma. Devuelve la dirección si la encuentra en este tramo.
static u32 ScanPlayerStep() {
    u32 end = s_pCursor + SCAN_STEP;
    if (end > SCAN_HI) end = SCAN_HI;
    u32 found = 0;
    for (u32 page = s_pCursor; page < end && !found; page += 0x1000) {
        if (!PageOk(page)) continue;
        for (u32 a = page; a < page + 0x1000; a += 4) {
            u16 z = *(volatile u16 *)a;
            if (z == 0 || z >= 600) continue;
            u32 off = a - page;
            if (off >= 0xE4 && off + 0x1C < 0x1000) {           // todo dentro de esta página ya validada
                if (PlayerSigRaw(a)) { found = a; break; }
            } else if (LooksLikePlayerBlock(a)) { found = a; break; }
        }
    }
    s_pCursor = (end >= SCAN_HI) ? SCAN_LO : end;
    return found;
}

// Firma de la cámara: aspecto exacto 400/240, fov plausible antes y matriz coherente 0xA8 antes.
static inline bool CameraSigRaw(u32 aspAddr) {
    float asp = *(volatile float *)aspAddr;
    float fov = *(volatile float *)(aspAddr - 4);
    if (std::fabs(asp - ASPECT_TOP) > 1e-4f || !(fov > 0.15f && fov < 1.7f)) return false;
    const volatile float *m = (const volatile float *)(aspAddr - 0xA8);
    for (int i = 0; i < 16; i++) if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1e6f) return false;
    float expect = (1.0f / tanf(fov * 0.5f)) / asp;
    if (std::fabs(m[0] - expect) > 0.02f * expect) return false;
    float l = m[12] * m[12] + m[13] * m[13] + m[14] * m[14];      // fila w = -dirección de la cámara (unitaria)
    return std::fabs(l - 1.0f) < 0.02f;
}

static bool LooksLikeCamera(u32 aspAddr) {
    if ((aspAddr & 3) || aspAddr < SCAN_LO + 0xB0 || aspAddr + 0x20 > SCAN_HI) return false;
    if (!PageOk(aspAddr) || !PageOk(aspAddr - 0xA8) || !PageOk(aspAddr + 0x14)) return false;
    return CameraSigRaw(aspAddr);
}

// La cámara correcta es la que SIGUE al jugador: su objetivo (+0xB4 desde la matriz) está sobre él.
// (En memoria quedan cámaras viejas, p. ej. la del Centro Pokémon, que siguen cumpliendo la firma.)
static volatile float s_plX = 0, s_plY = 0, s_plH = 0;
static bool CameraFollowsPlayer(u32 mat) {
    if (!PageOk(mat + 0xB4) || !PageOk(mat + 0xBC)) return false;
    float tx = *(volatile float *)(mat + 0xB4), th = *(volatile float *)(mat + 0xB8), ty = *(volatile float *)(mat + 0xBC);
    if (!Plausible(tx) || !Plausible(ty) || !Plausible(th)) return false;
    float dx = tx - s_plX, dy = ty - s_plY;
    return dx * dx + dy * dy < 160.f * 160.f && std::fabs(th - s_plH) < 200.f;
}

static u32 ScanCameraStep() {
    u32 end = s_cCursor + SCAN_STEP;
    if (end > SCAN_HI) end = SCAN_HI;
    u32 found = 0;
    for (u32 page = s_cCursor; page < end && !found; page += 0x1000) {
        if (!PageOk(page)) continue;
        for (u32 a = page; a < page + 0x1000; a += 4) {
            if (*(volatile u32 *)a != ASPECT_BITS) continue;
            if (LooksLikeCamera(a) && CameraFollowsPlayer(a - 0xA8)) { found = a - 0xA8; break; }
        }
    }
    s_cCursor = (end >= SCAN_HI) ? SCAN_LO : end;
    return found;
}

// ---------------------------------------------------------------------------------------------
static const float kYaw8[8] = {
    3.14159265f, 0.0f, -1.5707963f, 1.5707963f,      // N, S, O, E
    -2.3561945f, 2.3561945f, -0.7853982f, 0.7853982f // NO, NE, SO, SE
};

static volatile bool s_camGood = false;
// Cámaras ya vistas (cada mapa/interior reutiliza la suya): al cambiar de lugar se prueban antes de escanear
static u32 s_camKnown[8];
static int s_camKnownN = 0;
static void RememberCam(u32 c) {
    for (int i = 0; i < s_camKnownN; i++) if (s_camKnown[i] == c) return;
    if (s_camKnownN < 8) s_camKnownN++;
    for (int i = s_camKnownN - 1; i > 0; i--) s_camKnown[i] = s_camKnown[i - 1];
    s_camKnown[0] = c;
}
static u32 TryKnownCams() {
    for (int i = 0; i < s_camKnownN; i++) {
        u32 c = s_camKnown[i];
        if (LooksLikeCamera(c + 0xA8) && CameraFollowsPlayer(c)) return c;
    }
    return 0;
}
static void MaintainCamera(u64 now, bool canSearch) {
    bool sig = s_cam && LooksLikeCamera(s_cam + 0xA8);
    bool follows = sig && CameraFollowsPlayer(s_cam);
    s_camGood = follows;
    if (follows) { s_camBadSince = 0; return; }
    if (canSearch) {                                   // ¿es una cámara que ya conocemos? (instantáneo)
        u32 k = TryKnownCams();
        if (k) { s_cam = k; s_camBadSince = 0; s_camGood = true; return; }
    }
    if (sig && !follows && canSearch) {                // cámara vieja (otro mapa): buscar la nueva ya
        s_cam = 0;
        s_camBadSince = 0;
        s_cRestAt = 0;
    }
    if (s_cam && !s_camBadSince) s_camBadSince = now;
    // durante cargas/combates la cámara deja de cumplir la firma un rato: no buscar otra enseguida
    if (s_cam && now - s_camBadSince < 5000) return;
    if (!canSearch || now < s_cRestAt) return;
    u32 c = ScanCameraStep();
    if (!c && s_cCursor == SCAN_LO) s_cRestAt = now + 2000;
    if (c) {
        if (c != s_cam) Log::Write("Camara en 0x%08lX", (unsigned long)c);
        RememberCam(c);
        s_cam = c;
        s_camBadSince = 0;
        s_camGood = true;
    }
}

void Update(u64 nowMs) {
    LocalState st = {};
    st.stamp = nowMs;

    // 1) La cadena del juego manda: si da un bloque válido, se usa (y corrige uno viejo del escaneo)
    bool chainOk = false;
    u32 chainP = FindPlayerChain(chainOk);
    if (chainP && chainP != s_P) {
        Log::Write("Bloque del jugador en 0x%08lX (cadena)", (unsigned long)chainP);
        s_P = chainP;
        s_pMisses = 0;
    }
    // 2) Seguir el bloque conocido; si deja de ser válido varios ticks, buscarlo de nuevo
    if (s_P && !StillPlayerBlock(s_P)) {
        if (++s_pMisses > 15) { Log::Write("Bloque del jugador perdido"); s_P = 0; s_pMisses = 0; }
    } else if (s_P) {
        s_pMisses = 0;
    }
    // 3) Respaldo: escaneo por firma, solo si la cadena no está disponible
    if (!s_P && !chainOk) {
        u32 P = 0;
        if (nowMs >= s_pRestAt) {
            P = ScanPlayerStep();
            if (!P && s_pCursor == SCAN_LO) s_pRestAt = nowMs + 2000;   // barrido completo sin éxito
        }
        if (P) { s_P = P; s_pMisses = 0; Log::Write("Bloque del jugador en 0x%08lX (escaneo)", (unsigned long)P); }
    }

    bool haveP = s_P && s_pMisses == 0;
    ApplyState(st, s_P, haveP);
    if (haveP) {
        u32 P = s_P;
        st.valid = true;
        st.zone = *(volatile u16 *)P;
        st.facing8 = *(volatile u8 *)(P + 2) & 7;
        u8 form = ReadForm();
        st.ride = (form == 1 || form == 2) ? form : 0;
        st.x = *(volatile float *)(P + 0x10);
        st.z = *(volatile float *)(P + 0x14);
        st.y = *(volatile float *)(P + 0x18);
        st.yaw = kYaw8[st.facing8];

        float sp = 0.f;
        if (s_pvalid && nowMs > s_pt) {
            float dx = st.x - s_px, dy = st.y - s_py;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 90.f) sp = d * 1000.f / (float)(nowMs - s_pt);
        }
        s_speed += (sp - s_speed) * 0.4f;
        s_px = st.x; s_py = st.y; s_pt = nowMs; s_pvalid = true;
        st.speed = s_speed;
        if (st.ride) st.moveState = MOVE_BIKE;          // v16: también parado (los amigos te ven sobre la bici)
        else if (s_speed < 5.f) st.moveState = MOVE_IDLE;
        else if (s_speed < 95.f) st.moveState = MOVE_WALK;
        else st.moveState = MOVE_RUN;
    } else {
        s_pvalid = false;
    }

    // La cámara solo se busca caminando libremente (en combates/cargas la firma no se cumple)
    if (st.valid) {
        s_plX = st.x; s_plY = st.y; s_plH = st.z;
        MaintainCamera(nowMs, !st.inBattle && st.playerState == STATE_FREE);
    } else {
        s_camGood = false;
    }

    Lock l(s_mutex);
    s_state = st;
}

LocalState Snapshot() {
    Lock l(s_mutex);
    return s_state;
}

void RefreshFlags(LocalState &st) {
    ApplyState(st, s_P, st.valid && s_P);
}

void RefreshPos(LocalState &st) {
    u32 P = s_P;
    if (!st.valid || !P || (P & 3) || !PageOk(P) || !PageOk(P + 0x1C)) return;
    u16 zone = *(volatile u16 *)P;
    float x = *(volatile float *)(P + 0x10), z = *(volatile float *)(P + 0x14), y = *(volatile float *)(P + 0x18);
    if (!Plausible(x) || !Plausible(y) || !Plausible(z) || zone >= 536) return;
    st.zone = zone; st.x = x; st.y = y; st.z = z;
    st.facing8 = *(volatile u8 *)(P + 2) & 7;
    st.yaw = kYaw8[st.facing8];
    { u8 form = ReadForm(); st.ride = (form == 1 || form == 2) ? form : 0; }   // 1/2 = las dos bicis
}

bool ReadCamera(CameraState &out) {
    out.valid = false;
    u32 cam = s_cam;
    if (!cam || !s_camGood || !LooksLikeCamera(cam + 0xA8)) return false;
    const volatile float *m = (const volatile float *)cam;
    for (int i = 0; i < 16; i++) out.viewProj.m[i] = m[i];
    out.fov = m[0x29];   // +0xA4
    out.valid = true;
    return true;
}

bool Project(const CameraState &cam, const Vec3 &p, float &sx, float &sy, float &w) {
    float c[4];
    M44Point(cam.viewProj, p, c);
    if (!(c[3] >= 1.0f) || !std::isfinite(c[0]) || !std::isfinite(c[1])) return false;   // detrás de la cámara o inválido
    float iw = 1.0f / c[3];
    sx = 200.0f + 200.0f * c[0] * iw;
    sy = 120.0f - 120.0f * c[1] * iw;
    w = c[3];
    return std::fabs(sx) < 100000.f && std::fabs(sy) < 100000.f;
}

u32 PlayerBlockAddr() { return s_P; }
u32 CameraAddr() { return s_cam; }

} // namespace Game
