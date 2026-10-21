#include "SkinTex.hpp"
#include "SkinColors.hpp"
#include "HeroAssets.hpp"
#include "App.hpp"
#include "Config.hpp"
#include "Protocol.hpp"
#include "Log.hpp"
#include "GameState.hpp"
#include "Perf.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <cstdlib>

using namespace CTRPluginFramework;

namespace SkinTex {

// v16.5: 16 colores (SkinColors.hpp, los mismos que ven tus amigos) y un color propio para la textura de la cabeza
// (gorro de Bruno / lazo de Aura). Cada textura tiene su color "efectivo": 0 = original, 1..15 = color
// (+ SkinColors::EFF_WHITE = también el blanco: el gorro de Bruno es blanco).
struct Target {                 // textura del protagonista que se puede recolorear
    const uint8_t *raw; uint32_t size; uint8_t fmt;
    bool head;                  // textura de la cabeza: usa el color del gorro / lazo
    uint32_t sigOff; uint32_t sig;
    uint8_t *recolored; int recoloredEff;   // copia pintada con ese color efectivo (-1 = ninguna todavía)
    uint32_t probe[8]; int nProbe;          // palabras donde esa copia pintada y la original NO coinciden
};
struct Copy { int target; u32 addr; };

static const int MAX_TARGETS = 4;
static const int MAX_COPIES = 16;
static Target s_t[MAX_TARGETS];
static int    s_tn = 0;
static const Hero::Model *s_model = nullptr;
static Copy   s_c[MAX_COPIES];
static int    s_cn = 0;
static int    s_applied = 0;              // clave escrita ahora en las copias del juego: ropa | gorro << 8 (0 = original)
static u64    s_nextVerify = 0;

// La memoria lineal primero (ahí la deja el juego: p. ej. 0x15079900), luego la VRAM
static const u32 kRegions[2][2] = { { 0x14000000, 0x1C000000 }, { 0x1F000000, 0x1F600000 } };

static inline bool IsRed(int r, int g, int b) { return r >= 100 && g * 2 <= r && b * 2 <= r + 24; }
// blanco o gris claro (el gorro de Bruno): mismos límites que Render3D en 5 bits (mínimo >= 16, diferencia <= 5)
static inline bool IsWhite(int r, int g, int b) {
    int mn = r < g ? r : g, mx = r > g ? r : g;
    if (b < mn) mn = b;
    if (b > mx) mx = b;
    return mn >= 128 && mx - mn <= 40;
}
static inline bool Hit(int r, int g, int b, int eff) { return IsRed(r, g, b) || ((eff & SkinColors::EFF_WHITE) && IsWhite(r, g, b)); }

static void TintRGB(int eff, int I, int &r, int &g, int &b) {
    const uint8_t *k = SkinColors::kTint[eff & 15];
    r = (k[0] * 8 + k[0] / 4) * I / 255; g = (k[1] * 8 + k[1] / 4) * I / 255; b = (k[2] * 8 + k[2] / 4) * I / 255;
}

// ETC1: recolorea las sub-bloques cuyo color base es rojo; reescribe en modo individual (RGB444)
static void RecolorEtc(uint8_t *blk, int eff) {
    uint32_t hi = blk[4] | (blk[5] << 8) | (blk[6] << 16) | ((uint32_t)blk[7] << 24);
    bool diff = (hi >> 1) & 1;
    int c[2][3];
    if (diff) {
        int r1 = (hi >> 27) & 31, g1 = (hi >> 19) & 31, b1 = (hi >> 11) & 31;
        int dr = (hi >> 24) & 7, dg = (hi >> 16) & 7, db = (hi >> 8) & 7;
        if (dr > 3) dr -= 8;
        if (dg > 3) dg -= 8;
        if (db > 3) db -= 8;
        int r2 = (r1 + dr) & 31, g2 = (g1 + dg) & 31, b2 = (b1 + db) & 31;
        c[0][0] = (r1 << 3) | (r1 >> 2); c[0][1] = (g1 << 3) | (g1 >> 2); c[0][2] = (b1 << 3) | (b1 >> 2);
        c[1][0] = (r2 << 3) | (r2 >> 2); c[1][1] = (g2 << 3) | (g2 >> 2); c[1][2] = (b2 << 3) | (b2 >> 2);
    } else {
        c[0][0] = ((hi >> 28) & 15) * 17; c[1][0] = ((hi >> 24) & 15) * 17;
        c[0][1] = ((hi >> 20) & 15) * 17; c[1][1] = ((hi >> 16) & 15) * 17;
        c[0][2] = ((hi >> 12) & 15) * 17; c[1][2] = ((hi >> 8) & 15) * 17;
    }
    bool any = false;
    for (int k = 0; k < 2; k++)
        if (Hit(c[k][0], c[k][1], c[k][2], eff)) { TintRGB(eff, c[k][0], c[k][0], c[k][1], c[k][2]); any = true; }
    if (!any) return;
    uint32_t n = hi & 0xFC;                                      // tablas (7..2); diff = 0
    n |= hi & 1;                                                 // flip
    auto q = [](int x) -> uint32_t { int t = (x + 8) / 17; return (uint32_t)(t < 0 ? 0 : t > 15 ? 15 : t); };
    n |= q(c[0][0]) << 28 | q(c[1][0]) << 24 | q(c[0][1]) << 20 | q(c[1][1]) << 16 | q(c[0][2]) << 12 | q(c[1][2]) << 8;
    blk[4] = (uint8_t)n; blk[5] = (uint8_t)(n >> 8); blk[6] = (uint8_t)(n >> 16); blk[7] = (uint8_t)(n >> 24);
}

static void Recolor(const Target &t, uint8_t *out, int eff) {
    memcpy(out, t.raw, t.size);
    if (t.fmt == 12) { for (uint32_t i = 0; i + 8 <= t.size; i += 8) RecolorEtc(out + i, eff); return; }
    if (t.fmt == 13) { for (uint32_t i = 0; i + 16 <= t.size; i += 16) RecolorEtc(out + i + 8, eff); return; }
    int bpp = t.fmt == 0 ? 4 : t.fmt == 1 ? 3 : (t.fmt >= 2 && t.fmt <= 4) ? 2 : 0;
    if (!bpp) return;
    for (uint32_t i = 0; i + bpp <= t.size; i += bpp) {
        uint8_t *p = out + i;
        int r, g, b;
        uint16_t w = (uint16_t)(p[0] | (bpp > 1 ? p[1] << 8 : 0));
        switch (t.fmt) {
            case 0: r = p[3]; g = p[2]; b = p[1]; break;
            case 1: r = p[2]; g = p[1]; b = p[0]; break;
            case 2: r = ((w >> 11) & 31) << 3; g = ((w >> 6) & 31) << 3; b = ((w >> 1) & 31) << 3; break;
            case 3: r = ((w >> 11) & 31) << 3; g = ((w >> 5) & 63) << 2; b = (w & 31) << 3; break;
            default: r = ((w >> 12) & 15) * 17; g = ((w >> 8) & 15) * 17; b = ((w >> 4) & 15) * 17; break;
        }
        if (!Hit(r, g, b, eff)) continue;
        TintRGB(eff, r, r, g, b);
        switch (t.fmt) {
            case 0: p[3] = (uint8_t)r; p[2] = (uint8_t)g; p[1] = (uint8_t)b; break;
            case 1: p[2] = (uint8_t)r; p[1] = (uint8_t)g; p[0] = (uint8_t)b; break;
            case 2: w = (uint16_t)((w & 1) | ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1)); p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); break;
            case 3: w = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); break;
            default: w = (uint16_t)((w & 15) | ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4)); p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
static bool Readable(u32 a, u32 len) {
    MemInfo mi; PageInfo pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) return false;
    return (mi.perm & MEMPERM_READ) && mi.state != MEMSTATE_FREE && a + len <= mi.base_addr + mi.size;
}
static bool Writable(u32 a, u32 len) {
    MemInfo mi; PageInfo pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) return false;
    return (mi.perm & MEMPERM_WRITE) && a + len <= mi.base_addr + mi.size;
}

// Clave de lo que quieres ver: ropa | gorro << 8 (0 = todo original)
static int WantKey() {
    if (!Cfg::showMySkin) return 0;
    int c = (Cfg::colorVariant > 0 && Cfg::colorVariant < SkinColors::COUNT) ? Cfg::colorVariant : 0;
    int a = (Cfg::accColor > 0 && Cfg::accColor < SkinColors::ACC_COUNT) ? Cfg::accColor : 0;
    if (!c && a <= 1) return 0;
    return c | (a << 8);
}

// Color efectivo de una textura para una clave: el cuerpo lleva el de la ropa; la cabeza, el del gorro / lazo
static int EffFor(const Target &t, int key) {
    int c = key & 0xFF, a = (key >> 8) & 0xFF;
    if (t.head) return (int)SkinColors::HeadEff((uint32_t)c, (uint32_t)a, s_model && s_model->male);
    return c < SkinColors::COUNT ? c : 0;
}

static void Prepare(const Hero::Model *M) {
    for (int k = 0; k < s_tn; k++) { free(s_t[k].recolored); s_t[k].recolored = nullptr; }
    s_tn = 0; s_cn = 0; s_applied = 0;
    s_model = M;
    if (!M) return;
    for (int i = 0; i < M->texCount && s_tn < MAX_TARGETS; i++) {
        const Hero::Texture &T = M->tex[i];
        bool face = false, head = false;                         // la textura de la cara (boca roja) no se toca
        for (int r = 0; r < M->rangeCount; r++) {
            if (M->ranges[r].texture != i) continue;
            if (M->ranges[r].isFace) face = true;
            if (M->ranges[r].isHead) head = true;
        }
        if (face) continue;
        if (!T.raw || T.rawSize < 64 || !T.px) continue;
        int red = 0, white = 0;
        for (int p = 0; p < T.w * T.h; p++) {
            uint16_t c = T.px[p];
            int r = ((c >> 11) & 31) << 3, g = ((c >> 6) & 31) << 3, b = ((c >> 1) & 31) << 3;
            if (IsRed(r, g, b)) red++;
            else if (head && M->male && IsWhite(r, g, b)) white++;
        }
        if (red < 40 && white < 40) continue;
        Target &t = s_t[s_tn++];
        t.raw = T.raw; t.size = T.rawSize; t.fmt = T.fmt;
        t.head = head;
        t.sigOff = 0; t.sig = 0;
        for (u32 o = 0; o + 4 <= t.size && o < 4096; o += 4) {
            u32 w = t.raw[o] | (t.raw[o + 1] << 8) | (t.raw[o + 2] << 16) | ((u32)t.raw[o + 3] << 24);
            if (w && w != 0xFFFFFFFFu) { t.sigOff = o; t.sig = w; break; }
        }
        t.recolored = (uint8_t *)malloc(t.size);                 // una vez: sin pedir memoria al cambiar de color
        t.recoloredEff = -1; t.nProbe = 0;
        Log::Write("skin: textura %d = %dx%d fmt %d, %d px rojos, %d blancos%s%s", s_tn - 1, T.w, T.h, T.fmt, red, white,
                   head ? " (cabeza)" : "", t.recolored ? "" : " SIN MEMORIA");
    }
    Log::Write("skin: %d texturas que se pueden recolorear", s_tn);
}

// Copia pintada de la textura k con el color efectivo eff (0 = la original). Se calcula una vez por color y, con
// ella, las palabras "testigo": posiciones repartidas por la textura donde la pintada y la original difieren.
// v16.5: antes la comprobación miraba 4 palabras fijas y en la ropa de Aura las 4 caían fuera de lo rojo (pintada =
// original en esos puntos): cuando el juego recargaba la textura, el plugin la seguía dando por pintada y la ropa se
// quedaba sin color (el lazo, en otra textura, sí volvía a pintarse).
static const uint8_t *Expected(int k, int eff) {
    Target &t = s_t[k];
    if (!eff) return t.raw;
    if (!t.recolored) return nullptr;
    if (t.recoloredEff != eff) {
        Recolor(t, t.recolored, eff);
        t.recoloredEff = eff;
        u32 nd = 0;
        for (u32 o = 0; o + 4 <= t.size; o += 4) if (memcmp(t.raw + o, t.recolored + o, 4)) nd++;
        t.nProbe = 0;
        if (nd) {
            u32 want = nd < 8 ? nd : 8, idx = 0;
            for (u32 o = 0; o + 4 <= t.size && t.nProbe < (int)want; o += 4) {
                if (!memcmp(t.raw + o, t.recolored + o, 4)) continue;
                if (idx == (u32)t.nProbe * nd / want + nd / (2 * want)) t.probe[t.nProbe++] = o;
                idx++;
            }
        }
    }
    return t.recolored;
}

// ---------------------------------------------------------------------------------------------
// Búsqueda CONTINUA en segundo plano: el juego carga una copia nueva de la textura en cada mapa.
//  - zona caliente: ±16 MB alrededor de la última copia encontrada (se recorre en ~0,5 s);
//  - barrido completo de la memoria lineal y la VRAM, por tramos, siempre en marcha.
// Cada copia nueva se pinta en el momento (si su contenido es exactamente el original).
static u32 s_hot = 0, s_hotCursor = 0, s_fullCursor = 0;
static int s_fullRegion = 0;

static bool SameAs(u32 a, const uint8_t *p, u32 n) { return p && !memcmp((const void *)a, p, n); }

// ¿La copia del juego en a coincide con p? 4 palabras repartidas + las testigo del color actual (pocas lecturas: leer
// la textura entera obligaba a Azahar a consultar la caché de la GPU en cada lectura, memory.cpp:653). La comparación
// completa, solo antes de escribir. Llamar después de Expected() del color con el que se compara.
static bool Looks(u32 a, const Target &t, const uint8_t *p) {
    if (!p || t.size < 16) return false;
    u32 n = t.size;
    u32 offs[4] = { 0, (n / 3) & ~3u, (2 * n / 3) & ~3u, (n - 4) & ~3u };
    for (int i = 0; i < 4; i++) if (memcmp((const void *)(a + offs[i]), p + offs[i], 4)) return false;
    for (int i = 0; i < t.nProbe; i++) if (memcmp((const void *)(a + t.probe[i]), p + t.probe[i], 4)) return false;
    return true;
}

static bool Paint(int i) {
    const Target &t = s_t[s_c[i].target];
    const uint8_t *src = Expected(s_c[i].target, EffFor(t, s_applied));
    if (!src || !Writable(s_c[i].addr, t.size)) return false;
    memcpy((void *)s_c[i].addr, src, t.size);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_c[i].addr, t.size);
    return true;
}

static void LogRepaint(int k, u32 a) {
    static u64 next = 0;                                         // como mucho una línea cada 2 s
    u64 now = osGetTime();
    if (now < next) return;
    next = now + 2000;
    Log::Write("skin: el juego recargó la textura %d en 0x%08lX: repintada", k, (unsigned long)a);
}

static void AddCopy(int k, u32 a) {
    for (int i = 0; i < s_cn; i++) if (s_c[i].addr == a) return;
    if (s_cn >= MAX_COPIES) {                                    // lleno: descartar la más vieja
        memmove(&s_c[0], &s_c[1], sizeof(Copy) * (MAX_COPIES - 1));
        s_cn = MAX_COPIES - 1;
    }
    s_c[s_cn].target = k; s_c[s_cn].addr = a;
    if (s_applied) Paint(s_cn);
    s_cn++;
    s_hot = a;
    Log::Write("skin: textura %d en 0x%08lX (%d copias)", k, (unsigned long)a, s_cn);
}

// Recorre [cursor, cursor + budget) dentro de [lo, hi); devuelve el nuevo cursor
static u32 ScanRange(u32 lo, u32 hi, u32 cursor, u32 budget) {
    if (cursor < lo || cursor >= hi) cursor = lo;
    u32 stop = cursor + budget;
    if (stop > hi) stop = hi;
    while (cursor < stop) {
        MemInfo mi; PageInfo pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, cursor)) || !mi.size) { cursor += 0x1000; continue; }
        u32 blockEnd = mi.base_addr + mi.size;
        if (!(mi.perm & MEMPERM_READ) || mi.state == MEMSTATE_FREE) { cursor = blockEnd > cursor ? blockEnd : cursor + 0x1000; continue; }
        u32 end = blockEnd > stop ? stop : blockEnd;
        for (u32 a = (cursor + 0x7F) & ~0x7Fu; a < end; a += 0x80) {
            for (int k = 0; k < s_tn; k++) {
                const Target &t = s_t[k];
                if (a + t.size > blockEnd) continue;
                if (*(volatile u32 *)(a + t.sigOff) != t.sig) continue;
                if (memcmp((const void *)a, t.raw, t.size)) continue;
                AddCopy(k, a);
            }
        }
        cursor = end;
    }
    return cursor >= hi ? lo : cursor;
}

// v16.2: búsqueda DIRECTA, casi instantánea. Las direcciones de las texturas que dibuja tu personaje están en una tabla
// de su modelo: MoveModel del jugador (ObjID 0xFF) +0x8B8 = modelo, +0x6C = tabla de direcciones (comprobado en vivo
// con GDB 2026-10-17: 5 texturas x 3 entradas; la ropa en 0x1527E480 y 0x15296480). Por si otro modelo (bici...)
// la tuviera en otro campo, se miran todos los punteros del modelo (un nivel), y una dirección solo se acepta si su
// contenido es EXACTAMENTE la textura original. Antes solo había el barrido lento: hasta ~35 s por vuelta, y a los 8 s
// de llegar a un mapa dejaba de buscar, así que a veces la ropa se quedaba sin color.
static const u32 GS_PTR = 0x0062F7C4, GS_GM = 0x1C, GM_MMM = 0x20;
static const u32 MMM_SLOTS = 0x369C, MMM_CAP = 0x36A0, MM_STRIDE = 0xAB0, MM_MODEL = 0x8B8;
static inline bool HeapP(u32 a) { return a >= 0x08000000 && a < 0x10000000 && !(a & 3); }
static inline bool Rd32(u32 a, u32 &v) { if (!Readable(a, 4)) return false; v = *(volatile u32 *)a; return true; }

static u32 PlayerModel() {
    u32 gs, gm, mmm, slots, cap;
    if (!Rd32(GS_PTR, gs) || !HeapP(gs) || !Rd32(gs + GS_GM, gm) || !HeapP(gm) || !Rd32(gm + GM_MMM, mmm) || !HeapP(mmm) ||
        !Rd32(mmm + MMM_SLOTS, slots) || !HeapP(slots) || !Rd32(mmm + MMM_CAP, cap)) return 0;
    cap &= 0xFFFF;
    if (cap > 64) cap = 64;
    for (u32 i = 0; i < cap; i++) {
        u32 m = slots + i * MM_STRIDE, f, id, root;
        if (!Rd32(m + 4, f) || !(f & 1) || !Rd32(m + 0xC, id) || (id & 0xFFFF) != 0xFF) continue;
        return Rd32(m + MM_MODEL, root) && HeapP(root) ? root : 0;
    }
    return 0;
}

// ¿v es una copia de alguna textura del personaje? (las del juego van alineadas a 0x80)
//  - ya conocida: si el juego la acaba de recargar (vuelve a ser la original), se repinta YA, sin esperar a la
//    comprobación periódica (v16.5);
//  - nueva: se acepta solo si es una copia intacta de la original.
static void TryCopy(u32 v) {
    if (v < 0x14000000 || v >= 0x1C000000 || (v & 0x7F)) return;
    for (int i = 0; i < s_cn; i++) {
        if (s_c[i].addr != v) continue;
        int k = s_c[i].target;
        const Target &t = s_t[k];
        int eff = EffFor(t, s_applied);
        if (eff && Expected(k, eff) && Readable(v, t.size) && Looks(v, t, t.raw) && SameAs(v, t.raw, t.size) && Paint(i))
            LogRepaint(k, v);
        return;
    }
    for (int k = 0; k < s_tn; k++) {
        const Target &t = s_t[k];
        if (!Readable(v, t.size) || *(volatile u32 *)(v + t.sigOff) != t.sig || memcmp((const void *)v, t.raw, t.size)) continue;
        AddCopy(k, v);
        return;
    }
}

static void FindByPointers() {
    u32 root = PlayerModel();
    if (!root || !Readable(root, 0x300)) return;
    u32 last = 0;
    for (u32 o = 0; o < 0x300; o += 4) {
        u32 p = *(volatile u32 *)(root + o);
        if (!HeapP(p) || p == last || !Readable(p, 0x80)) continue;
        last = p;
        u32 prev = 0;
        for (u32 q = 0; q < 0x80; q += 4) {
            u32 v = *(volatile u32 *)(p + q);
            if (v != prev) TryCopy(v);                       // la tabla repite cada dirección 3 veces
            prev = v;
        }
    }
}

static void ScanTick() {
    if (!s_tn) return;
    if (s_hot) {
        u32 lo = s_hot > 0x14000000 + 0x1000000 ? s_hot - 0x1000000 : 0x14000000;
        u32 hi = s_hot + 0x1000000 < 0x1C000000 ? s_hot + 0x1000000 : 0x1C000000;
        s_hotCursor = ScanRange(lo, hi, s_hotCursor, 0x80000);
    }
    s_fullCursor = ScanRange(kRegions[s_fullRegion][0], kRegions[s_fullRegion][1], s_fullCursor, 0x20000);
    if (s_fullCursor == kRegions[s_fullRegion][0]) s_fullRegion ^= 1;
}

static void RestoreCopies() {
    for (int i = 0; i < s_cn; i++) {
        int k = s_c[i].target;
        const Target &t = s_t[k];
        int eff = EffFor(t, s_applied);
        if (!eff) continue;
        const uint8_t *cur = Expected(k, eff);
        if (cur && Readable(s_c[i].addr, t.size) && SameAs(s_c[i].addr, cur, t.size) && Writable(s_c[i].addr, t.size)) {
            memcpy((void *)s_c[i].addr, t.raw, t.size);
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_c[i].addr, t.size);
        }
    }
    s_applied = 0;
}

void Update(u64 now) {
    if (App::GetAssetState() != App::ASSETS_OK) return;
    u16 mid = App::GameModelId();       // recolorear el personaje que el juego realmente dibuja
    const Hero::Model *M = Hero::Get(mid == MMO_MODEL_MAY ? Hero::MODEL_MAY : Hero::MODEL_BRENDAN);
    if (M != s_model) {                                          // cambió el personaje: restaurar y empezar de nuevo
        RestoreCopies();
        Prepare(M);
        s_hot = 0;
    }
    int want = WantKey();

    // 1) verificar copias (cada 250 ms, unas pocas palabras): quitar las que ya no son la textura, repintar las que el
    //    juego recargó (al volver de un menú, de un combate o al cambiar de mapa la puede reponer en el mismo sitio)
    if (now >= s_nextVerify) {
        s_nextVerify = now + 250;
        int alive = 0;
        for (int i = 0; i < s_cn; i++) {
            int k = s_c[i].target;
            const Target &t = s_t[k];
            if (!Readable(s_c[i].addr, t.size)) continue;
            int eff = EffFor(t, s_applied);
            const uint8_t *exp = Expected(k, eff);
            if (!exp) { s_c[alive++] = s_c[i]; continue; }       // sin copia pintada (memoria): se deja como está
            bool painted = eff && Looks(s_c[i].addr, t, exp);
            bool orig = !painted && Looks(s_c[i].addr, t, t.raw);
            if (!painted && !orig) continue;                     // memoria reutilizada por otra cosa: NO tocar
            s_c[alive++] = s_c[i];
            // el juego la recargó intacta: volver a pintar (comparación completa antes de escribir)
            if (orig && eff && SameAs(s_c[i].addr, t.raw, t.size) && Paint(alive - 1)) LogRepaint(k, s_c[i].addr);
        }
        if (alive != s_cn) Log::Write("skin: %d de %d copias siguen", alive, s_cn);
        s_cn = alive;
    }
    // 2) cambio de color: solo se escribe donde el contenido es EXACTAMENTE el esperado (por textura: el cuerpo y la
    //    cabeza pueden llevar colores distintos)
    if (want != s_applied) {
        for (int k = 0; k < s_tn; k++) {
            const Target &t = s_t[k];
            int eo = EffFor(t, s_applied), en = EffFor(t, want);
            if (eo == en) continue;
            const uint8_t *cur = Expected(k, eo);
            bool match[MAX_COPIES] = {};
            for (int i = 0; i < s_cn; i++)
                match[i] = s_c[i].target == k && cur && Readable(s_c[i].addr, t.size) && SameAs(s_c[i].addr, cur, t.size);
            const uint8_t *src = Expected(k, en);
            if (!src) continue;
            for (int i = 0; i < s_cn; i++) {
                if (!match[i] || !Writable(s_c[i].addr, t.size)) continue;
                memcpy((void *)s_c[i].addr, src, t.size);
                svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_c[i].addr, t.size);
            }
        }
        s_applied = want;
    }
    // 3) búsqueda de copias nuevas: el juego carga la textura al cambiar de mapa.
    //    a) directa por la tabla del modelo, 5 veces por segundo (10 tras una carga o un menú), también en "ahorro";
    //    b) el barrido lento de respaldo durante la carga y 8 s después, y SIEMPRE mientras no se conozca ninguna copia
    //       (antes paraba a los 8 s aunque no la hubiera encontrado y la ropa se quedaba sin color hasta otro mapa).
    static u64 s_scanUntil = 0, s_nextPtr = 0;
    static int s_lastWant = 0;
    Game::LocalState gs = Game::Snapshot();
    if (!gs.valid || gs.playerState != Game::STATE_FREE) s_scanUntil = now + 8000;
    if (want != s_lastWant) s_scanUntil = now + 30000;   // color recién elegido: hasta 30 s para encontrar la textura
    s_lastWant = want;
    if ((want || s_applied) && now >= s_nextPtr) { s_nextPtr = now + (now < s_scanUntil ? 100 : 200); FindByPointers(); }
    if ((want || s_applied) && Perf::Level() < 2 && (now < s_scanUntil || !s_cn)) ScanTick();
}

bool Active() {
    int key = s_applied;
    if (!s_cn || !key || key != WantKey()) return false;
    for (int k = 0; k < s_tn; k++) if (EffFor(s_t[k], key)) return true;
    return false;
}

void RestoreAll() { RestoreCopies(); }

} // namespace SkinTex
