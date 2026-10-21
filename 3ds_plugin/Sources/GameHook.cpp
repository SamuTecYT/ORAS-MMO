#include "GameHook.hpp"
#include "GameState.hpp"
#include "Log.hpp"
#include "SkinTex.hpp"
#include "App.hpp"
#include "FriendMotion.hpp"
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>

using namespace CTRPluginFramework;

namespace GameHook {

// ---------------------------------------------------------------------------------------------
// Direcciones (KB, build verificada: §1, §5, §8). Todas de code.bin salvo las de DllField (relativas a su base).
static const u32 GS_STATIC        = 0x0062F7C4;
static const u32 FN_GS_UPDATE     = 0x00108A48;   // GameSystem_Update(GS*) una vez por cuadro, hilo del juego
static const u32 DLLFIELD_LIT     = 0x0019C354;   // literal del stub -> DllField 0x2708EE38
static const u32 DLLFIELD_OFF     = 0x0008EE38;
static const u32 RO_UNRESOLVED    = 0x0050DDA8;
static const u32 COMM_HDR         = 0x005871BC;   // cabecera por defecto del "compañero" (48 B)
static const u32 FN_ADD_MODEL_RES = 0x003F5830;   // MM*(MMM*, const u16 hdr[24], u16 zone, DressUp*)
static const u32 FN_REG_RESIDENT  = 0x003F60FC;   // (MMM*, MM*)
static const u32 FN_DEL_MODEL     = 0x003F6C70;   // (MMM*, MM*)
static const u32 FN_GET_BY_OBJID  = 0x004E3674;   // MM*(MMM*, u16)
static const u32 FN_GET_DRAWMODEL = 0x004E3D94;   // dm*(MM*)
static const u32 FN_SET_TRANSFORM = 0x003F2354;   // (MM*, PosObj*)
static const u32 FN_COMMIT_PREV   = 0x003F2E04;   // (MM*)
static const u32 FN_SET_FLAGS_B   = 0x003F4918;   // (MM*, u32)
static const u32 FN_SET_DIR       = 0x003F11FC;   // (MM*, u8 dir 0..7)
static const u32 FN_SET_FLAGS4    = 0x003F1324;   // (MM*, u32) MM+4 |= f  (MoveModel_SetFlags4)
static const u32 FN_CLR_FLAGS4    = 0x003F1620;   // (MM*, u32) MM+4 &= ~f
static const u32 MM_FLAG_HIDDEN   = 4;            // el juego no lo dibuja (sigue chocando): Native_MdlSetVisible lo usa
static const u32 FN_SET_ACTION    = 0x003FAE50;   // (MM*, u16 code < 0x104, u8 dir 0..7 / 9)
static const u32 FN_ACTION_END    = 0x004E3FD0;   // bool(MM*)
static const u16 ACT_STAYWALK_8F  = 29, ACT_STAYWALK_4F = 30;   // andar/correr EN EL SITIO (solo animación)
static const u16 ACT_ICON_EXCLAM  = 56;                         // "!" sobre la cabeza
static const u16 ACT_STAYJUMP_16F = 32;                         // saltito en el sitio (emotes Feliz / Risa)
static const u32 DF_BUILD_POS     = 0x00086EF8;   // DllField: (out, terrCtx, terr, PosIn*)
static const u32 DF_DM_UPDATE     = 0x0008710C;   // DllField: (dm)
static const int MMM_SLOTS_OFF = 0x369C, MMM_CAP_OFF = 0x36A0, MM_STRIDE = 0xAB0;
static const u16 OBJID_BASE = 0xD0;                // amigo N -> ObjID 0xD0 + N (el juego usa 0xFC para su compañero)
static const int MAX_NATIVE = 6;                   // como mucho 6 modelos añadidos a la vez
static const int FREE_MARGIN = 4;                  // deja siempre 4 huecos libres de los 32 (el juego puede necesitarlos)

static bool  s_installed = false, s_buildOk = false;
static const char *s_status = "Apagadas";
static Hook  s_hook;
static Mutex s_mutex;

// ---------------------------------------------------------------------------------------------
static inline bool Ok(u32 a, u32 len = 4) {
    if (!a) return false;
    return Process::CheckAddress(a & ~0xFFFu, MEMPERM_READ) && Process::CheckAddress((a + len - 1) & ~0xFFFu, MEMPERM_READ);
}
static inline u32 R32(u32 a) { return *(volatile u32 *)a; }
static inline u16 R16(u32 a) { return *(volatile u16 *)a; }
static inline bool Heap(u32 a) { return a >= 0x08000000 && a < 0x10000000 && !(a & 3); }

// KB §1: las 4 palabras de GetGameSystem identifican la build estudiada
static bool BuildCheck() {
    static const u32 kWords[4] = { 0xE59F0004, 0xE5900000, 0xE12FFF1E, 0x0062F7C4 };
    if (!Ok(0x0014E348, 16)) return false;
    for (int i = 0; i < 4; i++) if (R32(0x0014E348 + i * 4) != kWords[i]) return false;
    // y la función que vamos a enganchar empieza como se espera: push {r4,r5,r6,lr}; mov r4,r0
    return Ok(FN_GS_UPDATE, 8) && R32(FN_GS_UPDATE) == 0xE92D4070 && R32(FN_GS_UPDATE + 4) == 0xE1A04000;
}

// ---------------------------------------------------------------------------------------------
// Estado del cuadro (solo en el gancho)
struct Frame { u32 gs, gm, fm, mmm, dllBase, evVt; bool field, free, shortcut, xmenu; u16 zone; };

static bool ReadFrame(u32 gs, Frame &f) {
    memset(&f, 0, sizeof(f));
    f.gs = gs;
    if (!Heap(gs) || !Ok(gs + 0x48)) return false;
    f.gm = R32(gs + 0x1C);
    f.fm = R32(gs + 0x48);
    u32 evm = R32(gs + 0x18);
    if (!Heap(f.gm) || !Ok(f.gm + 0x20) || !Heap(evm) || !Ok(evm)) return false;
    u32 ev = R32(evm);
    u32 lit = Ok(DLLFIELD_LIT) ? R32(DLLFIELD_LIT) : RO_UNRESOLVED;
    if (lit != RO_UNRESOLVED && lit > DLLFIELD_OFF) f.dllBase = lit - DLLFIELD_OFF;
    f.field = Heap(f.fm) && f.dllBase && !(f.dllBase & 0xFFF) && Ok(f.fm + 0x164);
    if (f.field) {
        f.mmm = R32(f.gm + 0x20);
        f.zone = R16(f.fm + 0xF0);
        if (!Heap(f.mmm) || !Ok(f.mmm + MMM_CAP_OFF)) f.field = false;
    }
    // v16.1: el menú Y (EventCallShortcutButton) no quita a nadie: el mapa sigue a la vista y no guarda ni viaja
    if (ev && Heap(ev) && Ok(ev)) f.evVt = R32(ev);
    f.shortcut = ev && f.dllBase && f.evVt == f.dllBase + Game::VT_SHORTCUT_OFF;
    // v16.5: el menú X tampoco (va en la pantalla de abajo; el mapa sigue arriba). Elegir algo en él (Guardar =
    // EventReport, Mochila, Pokédex...) pone OTRO evento encima: deja de contar como libre y los modelos se quitan
    // en ese mismo cuadro (GsUpdateHook), antes de guardar nada (KB §8.5).
    f.xmenu = ev && f.evVt == Game::VT_FIELDMENU;
    f.free = f.field && (ev == 0 || f.shortcut || f.xmenu);   // libre: sin diálogo, puerta, guardado ni combate
    return true;
}

// ---------------------------------------------------------------------------------------------
// Amigos nativos
struct Native { bool used, hidden, jumping; u8 id; u16 objId, zone; u32 mm; FriendMotion::State mot; };
static Native s_nat[MAX_NATIVE];
struct Want { bool valid, hide; u8 id; u16 zone, model; float x, y, h, speed; u8 dir, state; };
static Want s_want[MAX_MMO_PLAYERS];
static volatile bool s_shown[MAX_MMO_PLAYERS + 1];       // lo DIBUJA el juego (modelo real visible)
static volatile bool s_exclam[MAX_MMO_PLAYERS + 1];      // "!" pendiente (emote Sorpresa)
static volatile bool s_jump[MAX_MMO_PLAYERS + 1];        // saltito pendiente (emotes Feliz / Risa)

void OnEmote(u8 id, u8 emote) {
    if (id < 1 || id > MAX_MMO_PLAYERS) return;
    if (emote == 6) s_exclam[id] = true;                  // 6 = "Sorpresa" (Emotes.cpp, 1-based)
    if (emote == 1 || emote == 3) s_jump[id] = true;      // 1 = "Feliz", 3 = "Risa": salta de alegría
}

static u8 Dir8(float yaw) {
    // el modelo del juego usa la misma convención que PlayerWork (0..7); se elige la más cercana
    static const float kYaw8[8] = { 3.14159265f, 0.0f, -1.5707963f, 1.5707963f, -2.3561945f, 2.3561945f, -0.7853982f, 0.7853982f };
    int best = 0; float bd = 99.f;
    for (int d = 0; d < 8; d++) {
        float e = yaw - kYaw8[d];
        while (e > 3.14159265f) e -= 6.2831853f;
        while (e < -3.14159265f) e += 6.2831853f;
        e = fabsf(e);
        if (e < bd) { bd = e; best = d; }
    }
    return (u8)best;
}

static u64 s_feedTime = 0;

void FeedViews(const MMO::RemoteView *views, int n, u64 now) {
    if (!s_installed || !s_buildOk) return;
    Lock l(s_mutex);
    s_feedTime = now;
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        Want &w = s_want[i];
        w.valid = false;
        if (i >= n) continue;
        const MMO::RemoteView &v = views[i];
        if (!v.valid || (v.flags & (MMO_FLAG_TRANSITION | MMO_FLAG_HIDDEN | MMO_FLAG_IN_BATTLE))) continue;
        // v16.1: TODOS tienen modelo real (choca contigo, no lo atraviesas). Si con el del juego no se vería igual
        // (en bici, con ropa de color, o tu recolor comparte su textura) ese modelo queda INVISIBLE y el plugin lo
        // dibuja encima, en el mismo sitio y con las mismas físicas (FriendMotion).
        w.hide = v.state == MOVE_BIKE || v.variant != 0 || v.acc >= 2 || (SkinTex::Active() && v.model == App::GameModelId());
        w.valid = true; w.id = v.id; w.zone = v.zone; w.model = v.model;
        w.x = v.x; w.y = v.y; w.h = v.z; w.speed = v.speed; w.state = v.state; w.dir = Dir8(v.yaw);
    }
}

bool IsNative(u8 id) { return id <= MAX_MMO_PLAYERS && s_shown[id]; }

static int UsedSlots(u32 mmm) {
    u32 base = R32(mmm + MMM_SLOTS_OFF);
    u16 cap = R16(mmm + MMM_CAP_OFF);
    if (!Heap(base) || cap == 0 || cap > 64 || !Ok(base, cap * MM_STRIDE)) return 99;
    int used = 0;
    for (int i = 0; i < cap; i++) used += (R32(base + i * MM_STRIDE + 4) & 1) ? 1 : 0;
    return used + (32 - (cap < 32 ? cap : 32));       // capacidad menor que 32: cuenta como ocupada
}

static void Despawn(Native &n, const Frame &f, bool fieldAlive) {
    // solo si ese hueco SIGUE siendo nuestro (en uso y con nuestro ObjID): el juego pudo haberlo
    // liberado y dado a otro NPC; borrarlo dos veces o borrar uno ajeno = memoria rota o juego congelado
    if (n.used && fieldAlive && Heap(n.mm) && Ok(n.mm + 0xE) && (R32(n.mm + 4) & 1) && R16(n.mm + 0xC) == n.objId) {
        typedef void (*FnDel)(u32, u32);
        ((FnDel)FN_DEL_MODEL)(f.mmm, n.mm);
    }
    if (n.id <= MAX_MMO_PLAYERS) { s_shown[n.id] = false; s_exclam[n.id] = false; s_jump[n.id] = false; }
    memset(&n, 0, sizeof(n));
}

static void DespawnAll(const Frame &f, bool fieldAlive) {
    for (auto &n : s_nat) if (n.used) Despawn(n, f, fieldAlive);
}

// Copia exacta de lo que hace el script MdlSetPos (DllField 0x2707F468): posición instantánea
static void PlaceModel(const Frame &f, u32 mm, float x, float h, float z, u8 dir) {
    u32 terr = R32(f.fm + 0x10C);
    if (!Heap(terr) || !Ok(terr + 0x7C)) return;
    struct PosIn { u16 type, pad; float x, y, z, unk; float fwd[3]; float up[3]; float up2[3]; } in;
    memset(&in, 0, sizeof(in));
    in.type = 0; in.x = x; in.y = h; in.z = z;
    in.fwd[2] = 1.f; in.up[1] = 1.f; in.up2[1] = 1.f;
    u8 out[0x40] __attribute__((aligned(8)));
    memset(out, 0, sizeof(out));
    typedef void *(*FnBuild)(void *, u32, u32, void *);
    typedef void (*FnMM)(u32);
    typedef void (*FnMMp)(u32, void *);
    typedef void (*FnMMu)(u32, u32);
    typedef u32 (*FnGetDm)(u32);
    ((FnBuild)(f.dllBase + DF_BUILD_POS))(out, R32(terr + 0x7C), terr, &in);
    ((FnMMp)FN_SET_TRANSFORM)(mm, out);
    ((FnMM)FN_COMMIT_PREV)(mm);
    u32 dm = ((FnGetDm)FN_GET_DRAWMODEL)(mm);
    if (dm) ((FnMM)(f.dllBase + DF_DM_UPDATE))(dm);
    ((FnMMu)FN_SET_FLAGS_B)(mm, 0x40000000);
    ((FnMMu)FN_SET_DIR)(mm, dir);
}

static void UpdateNatives(const Frame &f) {
    Want want[MAX_MMO_PLAYERS];
    { Lock l(s_mutex); memcpy(want, s_want, sizeof(want)); }
    // si el OSD dejó de informar (pausa, menú del plugin), los datos están viejos: nadie se queda congelado
    if ((s64)(osGetTime() - s_feedTime) > 300) for (auto &w : want) w.valid = false;
    // 1) borrar los que ya no deben estar (se fue, cambió de zona, se ocultó)
    for (auto &n : s_nat) {
        if (!n.used) continue;
        bool keep = n.zone == f.zone && Heap(n.mm) && Ok(n.mm + 4) && (R32(n.mm + 4) & 1) && R16(n.mm + 0xC) == n.objId;
        bool wanted = false;
        for (auto &w : want) if (w.valid && w.id == n.id && w.zone == f.zone) wanted = true;
        if (!keep || !wanted) Despawn(n, f, keep);
    }
    // 2) mover / crear (como mucho 1 creación por cuadro)
    bool spawned = false;
    for (auto &w : want) {
        if (!w.valid || w.zone != f.zone || w.id < 1 || w.id > MAX_MMO_PLAYERS) continue;
        Native *n = nullptr;
        for (auto &k : s_nat) if (k.used && k.id == w.id) n = &k;
        if (!n) {
            if (spawned) continue;
            Native *slot = nullptr;
            for (auto &k : s_nat) if (!k.used) { slot = &k; break; }
            if (!slot) continue;
            if (UsedSlots(f.mmm) > 32 - FREE_MARGIN - 1) continue;     // nunca llegar al límite (assert = congelado)
            u16 objId = (u16)(OBJID_BASE + w.id);
            typedef u32 (*FnGetById)(u32, u32);
            if (((FnGetById)FN_GET_BY_OBJID)(f.mmm, objId)) continue;   // la zona ya usa ese ObjID
            if (!Ok(COMM_HDR, 48)) continue;
            u16 hdr[24];
            memcpy(hdr, (const void *)COMM_HDR, 48);
            hdr[0] = objId;
            hdr[1] = (w.model == MMO_MODEL_MAY) ? 171 : 172;   // HEROINE_RS / HERO_RS
            hdr[3] = 0;                                          // sin evento...
            hdr[5] = 0;                                          // ...ni script: hablarle (A) no dispara nada
            hdr[6] = w.dir;
            u16 gx = (u16)(w.x > 0 ? w.x / 18.f : 0), gz = (u16)(w.y > 0 ? w.y / 18.f : 0);
            memcpy((u8 *)hdr + 0x28, &gx, 2); memcpy((u8 *)hdr + 0x2A, &gz, 2); memcpy((u8 *)hdr + 0x2C, &w.h, 4);
            typedef u32 (*FnAdd)(u32, u16 *, u32, void *);
            typedef void (*FnReg)(u32, u32);
            u32 mm = ((FnAdd)FN_ADD_MODEL_RES)(f.mmm, hdr, f.zone, nullptr);
            spawned = true;
            if (!Heap(mm)) continue;
            ((FnReg)FN_REG_RESIDENT)(f.mmm, mm);
            slot->used = true; slot->id = w.id; slot->objId = objId; slot->zone = f.zone; slot->mm = mm;
            slot->hidden = false; slot->jumping = false;
            n = slot;
            Log::Write("nativo: J%u como modelo real (ObjID 0x%X, %d huecos usados%s)", w.id, objId, UsedSlots(f.mmm), w.hide ? ", invisible" : "");
        }
        // visible / invisible (bici, color): solo el bit "no dibujar" del juego, el choque sigue
        if (w.hide != n->hidden) {
            typedef void (*FnFlag)(u32, u32);
            ((FnFlag)(w.hide ? FN_SET_FLAGS4 : FN_CLR_FLAGS4))(n->mm, MM_FLAG_HIDDEN);
            n->hidden = w.hide;
        }
        // solo cuando el juego lo marca como listo (+0xA1C == 2) y ya tiene terreno (+0x8A0): antes, SetTransform
        // leería punteros vacíos. Mientras tanto se queda donde lo puso la cabecera (su casilla).
        if (!Ok(n->mm + 0xA1C) || *(volatile u8 *)(n->mm + 0xA1C) != 2 || !Heap(R32(n->mm + 0x8A0))) continue;
        // Su movimiento ya ocurrió en su consola (no se le puede frenar aquí): si por el retraso de la red
        // se mete en tu sitio, se queda justo al lado (FriendMotion: mismas físicas que el dibujo del plugin).
        bool hasMe = Ok(f.gm + 0x13C);
        float mx = hasMe ? *(volatile float *)(f.gm + 0x134) : 0.f, mz = hasMe ? *(volatile float *)(f.gm + 0x13C) : 0.f;
        FriendMotion::Step(n->mot, w.x, w.y, hasMe, mx, mz);
        typedef u32 (*FnEnd)(u32);
        typedef void (*FnAct)(u32, u32, u32);
        // mientras salta (quieto) el propio juego lo sube y lo baja: no se le recoloca hasta que termine
        if (n->jumping && (w.state != MOVE_IDLE || n->hidden || ((FnEnd)FN_ACTION_END)(n->mm))) n->jumping = false;
        if (!n->jumping) PlaceModel(f, n->mm, n->mot.x, w.h, n->mot.y, w.dir);
        if (n->hidden) { s_shown[w.id] = false; s_exclam[w.id] = false; s_jump[w.id] = false; continue; }   // lo dibuja el plugin
        // piernas: el juego anima "andar en el sitio" mientras nosotros lo movemos (se repite al terminar)
        bool idle = ((FnEnd)FN_ACTION_END)(n->mm);
        if (s_exclam[w.id] && idle) { ((FnAct)FN_SET_ACTION)(n->mm, ACT_ICON_EXCLAM, 9); s_exclam[w.id] = false; idle = false; }
        if (s_jump[w.id] && w.state != MOVE_IDLE) s_jump[w.id] = false;            // andando no se ve: se descarta
        if (s_jump[w.id] && idle) { ((FnAct)FN_SET_ACTION)(n->mm, ACT_STAYJUMP_16F, 9); s_jump[w.id] = false; n->jumping = true; idle = false; }
        if (w.state != MOVE_IDLE && w.speed > 6.f && idle)
            ((FnAct)FN_SET_ACTION)(n->mm, w.speed > 85.f ? ACT_STAYWALK_4F : ACT_STAYWALK_8F, w.dir);
        s_shown[w.id] = true;
    }
}

// ---------------------------------------------------------------------------------------------
static void OnFrame(u32 gs) {
    Frame f;
    if (!ReadFrame(gs, f)) return;
    static u32 lastFm = 0;
    static u16 lastZone = 0xFFFF;
    bool fmChanged = f.fm != lastFm, zoneChanged = f.zone != lastZone;
    lastFm = f.fm; lastZone = f.zone;
    // diagnóstico: qué evento del juego empieza (vtable relativa a DllField si es suya), como mucho 1 por segundo
    static u32 lastVt = 0;
    static u64 nextEvLog = 0;
    if (f.evVt != lastVt && osGetTime() >= nextEvLog) {
        lastVt = f.evVt; nextEvLog = osGetTime() + 1000;
        if (f.evVt) {
            bool inDll = f.dllBase && f.evVt >= f.dllBase && f.evVt < f.dllBase + 0x200000;
            Log::Write("evento: vt=0x%08lX (%s0x%lX)%s", (unsigned long)f.evVt, inDll ? "DllField+" : "",
                       (unsigned long)(inDll ? f.evVt - f.dllBase : f.evVt), f.shortcut ? " menu Y: los amigos se quedan" : f.xmenu ? " menu X: los amigos se quedan" : "");
        }
    }
    if (!f.free || fmChanged || zoneChanged) {
        // cualquier evento (menú, guardar, puerta, combate) o cambio de zona: fuera los modelos añadidos.
        // Si el Fieldmap cambió (mapa nuevo) solo se olvidan: el juego ya destruyó los del mapa anterior.
        DespawnAll(f, f.field && !fmChanged);
        if (!f.free) return;
    }
    UpdateNatives(f);
}

static u32 GsUpdateHook(u32 gs) {
    OnFrame(gs);
    u32 r = HookContext::GetCurrent().OriginalFunction<u32>(gs);
    // si en este mismo cuadro empezó un evento (combate, puerta, script), fuera los modelos YA (KB §8.5)
    Frame f;
    if (ReadFrame(gs, f) && !f.free) {
        bool any = false;
        for (auto &n : s_nat) any |= n.used;
        if (any) DespawnAll(f, f.field);
    }
    return r;
}

bool Install() {
    if (s_installed) return s_buildOk;
    s_installed = true;
    s_buildOk = BuildCheck();
    if (!s_buildOk) { s_status = "Juego distinto"; Log::Write("nativo: build distinta, funciones nativas desactivadas"); return false; }
    s_hook.InitializeForMitm(FN_GS_UPDATE, (u32)GsUpdateHook);
    HookResult r = s_hook.Enable();
    if (r != HookResult::Success) { s_buildOk = false; s_status = "No se pudo activar"; Log::Write("nativo: gancho fallo (%d)", (int)r); return false; }
    s_status = "Activas";
    Log::Write("nativo: gancho en GameSystem_Update instalado");
    return true;
}

bool Active() { return s_installed && s_buildOk; }
const char *Status() { return s_status; }

} // namespace GameHook
