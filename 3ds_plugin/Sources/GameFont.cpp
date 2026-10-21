#include "GameFont.hpp"
#include "GameText.hpp"
#include "Lang.hpp"
#include "Log.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace CTRPluginFramework;

namespace GameFont {

// CTRPF dibuja la fuente del sistema reduciendo cada celda (25x30) a 14x16 y mide con el factor 14/25: se imita
// para que estas letras midan y se alineen igual que las suyas.
static const int GW = 14, GH = 16;
static const int SRC_W = 25, SRC_H = 30;
static const float SCALE = 14.f / 25.f;

struct Glyph { u16 cp; s8 x; u8 adv; u8 a[GW * GH / 2]; };   // a: 4 bits por punto
struct Set { Glyph *g; int n; };
static Set s_sets[2];                    // 0 = japonés, 1 = coreano (nunca se liberan)
static bool s_tried[2] = { false, false };   // ya se intentó preparar (solo lo usa el hilo de recursos)

static inline u16 Rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 Rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

// UTF-8 -> código (no hacen falta letras de 4 bytes: U+FFFD)
static u32 Next(const u8 *&p) {
    u32 c = *p++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && (p[0] & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; return c; }
    if ((c & 0xF0) == 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
        return c;
    }
    while ((*p & 0xC0) == 0x80) p++;
    return 0xFFFD;
}

static int SlotFor(Lang::Id u) { return u == Lang::JA ? 0 : u == Lang::KO ? 1 : -1; }

// Letras del idioma elegido, si ya están (-1 = ninguno o no es japonés/coreano). No usa Lang::Shown (que pregunta aquí).
static int ActiveSlot() {
    int s = SlotFor(Lang::Ui());
    return (s >= 0 && s_sets[s].g) ? s : -1;
}

static const Glyph *Find(const Set &S, u32 cp) {
    int lo = 0, hi = S.n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        u32 v = S.g[m].cp;
        if (v == cp) return &S.g[m];
        if (v < cp) lo = m + 1; else hi = m - 1;
    }
    return nullptr;
}

// ---- Lectura del .bcfnt ----------------------------------------------------------------------
struct Font {
    u64 at; u32 len;                       // dónde está el archivo dentro de la RomFS
    u8 cw, ch; u16 rows, lines, sw, sh, nSheets;
    u32 sheetSize, sheetData, cwdh, cmap;  // posiciones dentro del archivo
    u8 *tail; u32 tailOff, tailLen;        // anchos y mapa de letras (van detrás de las hojas)
    s8 defLeft; u8 defChar;
};

static const u8 *TailAt(const Font &F, u32 off, u32 need) {
    if (off < F.tailOff || off - F.tailOff > F.tailLen || need > F.tailLen - (off - F.tailOff)) return nullptr;
    return F.tail + (off - F.tailOff);
}

static int GlyphIndex(const Font &F, u32 cp) {
    u32 off = F.cmap;
    for (int guard = 0; off && guard < 64; guard++) {
        const u8 *b = TailAt(F, off, 12);
        if (!b) return -1;
        u32 start = Rd16(b), end = Rd16(b + 2), method = Rd16(b + 4), nxt = Rd32(b + 8);
        if (cp >= start && cp <= end) {
            if (method == 0) {
                const u8 *d = TailAt(F, off + 12, 2);
                return d ? (int)(Rd16(d) + (cp - start)) : -1;
            }
            if (method == 1) {
                const u8 *d = TailAt(F, off + 12 + (cp - start) * 2, 2);
                if (!d) return -1;
                u16 v = Rd16(d);
                return v == 0xFFFF ? -1 : v;
            }
            if (method == 2) {                          // pares (código, letra) ordenados por código
                const u8 *d = TailAt(F, off + 12, 2);
                if (!d) return -1;
                int n = Rd16(d);
                const u8 *e = TailAt(F, off + 14, (u32)n * 4);
                if (!e) return -1;
                int lo = 0, hi = n - 1;
                while (lo <= hi) {
                    int m = (lo + hi) >> 1;
                    u32 c = Rd16(e + m * 4);
                    if (c == cp) return Rd16(e + m * 4 + 2);
                    if (c < cp) lo = m + 1; else hi = m - 1;
                }
            }
        }
        off = nxt;
    }
    return -1;
}

static void Widths(const Font &F, int gi, s8 &left, u8 &charW) {
    u32 off = F.cwdh;
    for (int guard = 0; off && guard < 64; guard++) {
        const u8 *b = TailAt(F, off, 8);
        if (!b) break;
        int s = Rd16(b), e = Rd16(b + 2);
        if (gi >= s && gi <= e) {
            const u8 *w = TailAt(F, off + 8 + (u32)(gi - s) * 3, 3);
            if (!w) break;
            left = (s8)w[0]; charW = w[2];
            return;
        }
        off = Rd32(b + 4);
    }
    left = F.defLeft; charW = F.defChar;
}

static bool OpenFont(Hero::ByteSource &fs, int entry, Font &F) {
    memset(&F, 0, sizeof(F));
    u64 gOff = 0, gSize = 0;
    if (!fs.Find("/a/1/6/7", gOff, gSize)) return false;
    if (!Hero::GarcRange(fs, gOff, gSize, (u32)entry, F.at, F.len)) return false;
    u8 h[0x60];
    if (F.len < 0x400 || !fs.Read(F.at, h, sizeof(h))) return false;
    if (memcmp(h, "CFNT", 4) || memcmp(h + 0x14, "FINF", 4)) return false;
    const u8 *fi = h + 0x14;
    F.defLeft = (s8)fi[0xC]; F.defChar = fi[0xE];
    u32 tglp = Rd32(fi + 0x10);
    F.cwdh = Rd32(fi + 0x14); F.cmap = Rd32(fi + 0x18);
    if (tglp < 0x34 || tglp + 0x18 > sizeof(h)) return false;
    const u8 *t = h + tglp;
    F.cw = t[0]; F.ch = t[1];
    F.sheetSize = Rd32(t + 4); F.nSheets = Rd16(t + 8);
    u16 fmt = Rd16(t + 0xA);
    F.rows = Rd16(t + 0xC); F.lines = Rd16(t + 0xE); F.sw = Rd16(t + 0x10); F.sh = Rd16(t + 0x12);
    F.sheetData = Rd32(t + 0x14);
    if (fmt != 11 || !F.cw || !F.ch || F.cw > 32 || F.ch > 32 || !F.rows || !F.lines || !F.nSheets) return false;   // 11 = A4
    if ((F.sw & 7) || (F.sh & 7) || F.sheetSize != (u32)F.sw * F.sh / 2 || F.sheetSize > 0x8000) return false;
    if ((u32)F.rows * (F.cw + 1) > F.sw || (u32)F.lines * (F.ch + 1) > F.sh) return false;
    F.tailOff = F.sheetData + (u32)F.nSheets * F.sheetSize;
    if (F.tailOff >= F.len || F.len - F.tailOff > 0x40000) return false;
    F.tailLen = F.len - F.tailOff;
    F.tail = (u8 *)malloc(F.tailLen);
    if (!F.tail) return false;
    if (!fs.Read(F.at + F.tailOff, F.tail, F.tailLen)) { free(F.tail); F.tail = nullptr; return false; }
    return true;
}

static inline u32 Morton(u32 x, u32 y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// Saca la letra 'gi' de su hoja (A4 en mosaicos de 8x8, orden Morton) y la reduce a 14x16 con 4x4 muestras por punto
static void Raster(const Font &F, const u8 *sheet, int gi, u8 *out) {
    int per = F.rows * F.lines, k = gi % per;
    int x0 = (k % F.rows) * (F.cw + 1) + 1, y0 = (k / F.rows) * (F.ch + 1) + 1;
    memset(out, 0, GW * GH / 2);
    for (int dy = 0; dy < GH; dy++)
        for (int dx = 0; dx < GW; dx++) {
            int sum = 0;
            for (int j = 0; j < 4; j++) {
                int sy = ((dy * 4 + j) * 2 + 1) * SRC_H / (GH * 8);
                if (sy >= F.ch) continue;
                for (int i = 0; i < 4; i++) {
                    int sx = ((dx * 4 + i) * 2 + 1) * SRC_W / (GW * 8);
                    if (sx >= F.cw) continue;
                    u32 px = x0 + sx, py = y0 + sy;
                    u32 tx = ((py >> 3) * (F.sw >> 3) + (px >> 3)) * 64 + Morton(px & 7, py & 7);
                    u8 v = sheet[tx >> 1];
                    sum += (tx & 1) ? (v >> 4) : (v & 15);
                }
            }
            int a = (sum * 5 / 4 + 8) / 16;           // un poco más intensa: la reducción aclara los trazos finos
            if (a > 15) a = 15;
            int idx = dy * GW + dx;
            out[idx >> 1] |= (u8)(a << ((idx & 1) * 4));
        }
}

// ---- Preparar las letras de un idioma (hilo de recursos) -------------------------------------
static u32 s_need[65536 / 32];            // qué letras hacen falta (solo lo usa el hilo de recursos)
static inline void Need(u32 c) { if (c < 65536) s_need[c >> 5] |= 1u << (c & 31); }
static void NeedText(const char *t) {
    if (!t) return;
    const u8 *p = (const u8 *)t;
    while (*p) Need(Next(p));
}

// v17.0 r2: con los nombres de los textos del juego de ESE idioma (GameText::Preload los carga todos al arrancar): antes
// se esperaba a que el jugador eligiera el idioma y a sus nombres, y mientras tanto el menú salía con "?".
static bool Build(Hero::ByteSource &fs, int slot) {
    u64 t0 = osGetTime();
    Lang::Id ui = slot ? Lang::KO : Lang::JA;
    memset(s_need, 0, sizeof(s_need));
    for (u32 c = 0x20; c < 0x7F; c++) Need(c);
    for (int i = 0; i < Lang::Rows(); i++) NeedText(Lang::Row(i, ui));
    NeedText("日本語 (かな)"); NeedText("日本語 (漢字)"); NeedText("한국어");   // nombres de los idiomas (Lang::Name)
    static const int kJa[] = { 1, 2 }, kKo[] = { 8 };
    const int *gis = slot ? kKo : kJa;
    int ngi = slot ? 1 : 2;
    for (int k = 0; k < ngi; k++)
        for (int i = 0; i < 1024; i++) { NeedText(GameText::SpeciesOf(gis[k], (u16)i)); NeedText(GameText::LocationOf(gis[k], (u16)i)); }
    NeedText("…•·×→←↑↓★♂♀「」『』（）！？、。・ー～");

    Font F;
    if (!OpenFont(fs, slot == 0 ? 1 : 0, F)) { Log::Write("letras del juego: no se pudo leer la fuente"); return false; }
    int maxN = 0;
    for (int w = 0; w < 65536 / 32; w++) maxN += __builtin_popcount(s_need[w]);
    Glyph *G = (Glyph *)malloc(sizeof(Glyph) * maxN);
    u8 *sheet = (u8 *)malloc(F.sheetSize);
    if (!G || !sheet) {
        free(G); free(sheet); free(F.tail);
        Log::Write("letras del juego: sin memoria (%d letras)", maxN);
        return false;
    }
    int n = 0, cur = -1, reads = 0, fails = 0, per = F.rows * F.lines;
    char miss[64] = "";                                  // las primeras que no están (para el registro)
    size_t ml = 0;
    for (u32 cp = 0x20; cp < 65536; cp++) {             // en orden de código: la lista queda ordenada
        if (!((s_need[cp >> 5] >> (cp & 31)) & 1)) continue;
        int gi = GlyphIndex(F, cp);
        if (gi < 0 || gi >= per * F.nSheets) {          // no está en esta fuente: irá con la del sistema
            if (ml + 6 < sizeof(miss)) ml += snprintf(miss + ml, sizeof(miss) - ml, " %lX", (unsigned long)cp);
            continue;
        }
        int sh = gi / per;
        if (sh != cur) {
            bool ok = false;
            for (int tr = 0; tr < 3 && !ok; tr++) {         // al arrancar el juego también lee: se reintenta
                ok = fs.Read(F.at + F.sheetData + (u64)sh * F.sheetSize, sheet, F.sheetSize);
                if (!ok) { fails++; svcSleepThread(5 * 1000000LL); }
            }
            if (!ok) { cur = -1; if (ml + 6 < sizeof(miss)) ml += snprintf(miss + ml, sizeof(miss) - ml, " !%lX", (unsigned long)cp); continue; }
            cur = sh; reads++;
        }
        s8 left; u8 charW;
        Widths(F, gi, left, charW);
        int xo = (int)floorf(left * SCALE);
        int xa = (int)roundf(charW * SCALE - left * SCALE);
        Glyph &g = G[n++];
        g.cp = (u16)cp; g.x = (s8)xo; g.adv = (u8)(xo + xa > 0 ? xo + xa : 0);
        Raster(F, sheet, gi, g.a);
    }
    free(sheet); free(F.tail);
    if (n < maxN && n > 0) { Glyph *r = (Glyph *)realloc(G, sizeof(Glyph) * n); if (r) G = r; }
    if (n <= 0) { free(G); return false; }
    s_sets[slot].n = n;
    __sync_synchronize();                                // n y las letras, antes que g (los demás hilos miran g)
    s_sets[slot].g = G;
    Log::Write("letras del juego (%s): %d de %d, %lu KB, %d hojas leidas (%d fallos), %lu ms; sin ellas:%s", slot ? "coreano" : "japones",
               n, maxN, (unsigned long)(sizeof(Glyph) * n / 1024), reads, fails, (unsigned long)(osGetTime() - t0), miss);
    return n > 0;
}

// Una letra preparada por llamada: primero el idioma elegido (si es japonés o coreano), después el otro. Desde la v17.0 r2
// el hilo de recursos lo llama al arrancar, justo después de GameText::Preload: al elegir el idioma ya está todo listo.
void Serve(Hero::ByteSource &fs) {
    int first = SlotFor(Lang::Ui());
    if (first < 0) first = 0;
    for (int k = 0; k < 2; k++) {
        int slot = k ? 1 - first : first;
        if (s_tried[slot]) continue;
        s_tried[slot] = true;
        Build(fs, slot);
        return;
    }
}

bool Ready(Lang::Id id) {
    int s = SlotFor(id);
    return s >= 0 && s_sets[s].g;
}

bool Active() { return ActiveSlot() >= 0; }

u32 Generation() { return (u32)(ActiveSlot() + 2); }   // cambia cuando cambia la fuente que se usa

int Width(const char *t) {
    int a = ActiveSlot();
    if (!t) return 0;
    if (a < 0) return (int)OSD::GetTextWidth(true, t);
    const Set &S = s_sets[a];
    int w = 0;
    const u8 *p = (const u8 *)t;
    while (*p) {
        const u8 *q = p;
        u32 cp = Next(p);
        const Glyph *g = Find(S, cp);
        if (g) { w += g->adv; continue; }
        char one[8];
        size_t k = (size_t)(p - q) < sizeof(one) - 1 ? (size_t)(p - q) : sizeof(one) - 1;
        memcpy(one, q, k); one[k] = 0;
        w += (int)OSD::GetTextWidth(true, one);
    }
    return w;
}

static void Blit(const Screen &s, const Glyph &g, int x0, int y0, const Color &c, int maxX) {
    for (int yy = 0; yy < GH; yy++) {
        int y = y0 + yy;
        if (y < 0 || y >= 240) continue;
        for (int xx = 0; xx < GW; xx++) {
            int x = x0 + xx;
            if (x < 0 || x >= maxX) continue;
            int i = yy * GW + xx;
            int a = (g.a[i >> 1] >> ((i & 1) * 4)) & 15;
            if (!a) continue;
            if (a == 15) { s.DrawPixel((u32)x, (u32)y, c); continue; }
            Color bg;
            s.ReadPixel((u32)x, (u32)y, bg);
            Color o((u8)((c.r * a + bg.r * (15 - a)) / 15), (u8)((c.g * a + bg.g * (15 - a)) / 15),
                    (u8)((c.b * a + bg.b * (15 - a)) / 15), 255);
            s.DrawPixel((u32)x, (u32)y, o);
        }
    }
}

void Draw(const Screen &s, const char *t, int x, int y, const Color &c, int maxX) {
    int a = ActiveSlot();
    if (!t) return;
    if (a < 0) { s.DrawSysfont(t, (u32)(x < 0 ? 0 : x), (u32)(y < 0 ? 0 : y), c); return; }
    const Set &S = s_sets[a];
    const u8 *p = (const u8 *)t;
    while (*p && x < maxX) {
        const u8 *q = p;
        u32 cp = Next(p);
        const Glyph *g = Find(S, cp);
        if (g) {
            if (cp != ' ') Blit(s, *g, x + g->x, y, c, maxX);
            x += g->adv;
            continue;
        }
        char one[8];                                     // no está en la fuente del juego: con la del sistema
        size_t k = (size_t)(p - q) < sizeof(one) - 1 ? (size_t)(p - q) : sizeof(one) - 1;
        memcpy(one, q, k); one[k] = 0;
        if (x >= 0) s.DrawSysfont(one, (u32)x, (u32)(y < 0 ? 0 : y), c);
        x += (int)OSD::GetTextWidth(true, one);
    }
}

} // namespace GameFont
