#include "Lang.hpp"
#include "GameFont.hpp"
#include "UiGfx.hpp"
#include "Render3D.hpp"
#include "Overlay.hpp"
#include "App.hpp"
#include "Log.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace CTRPluginFramework;

namespace UiGfx {

// ---------------------------------------------------------------------------------------------
void Spring::Step(float dt, float response, float damping) {
    if (!init) { x = target; v = 0.f; init = true; return; }
    if (dt <= 0.f) return;
    if (dt > 0.1f) dt = 0.1f;                                     // pausa larga: sin saltos
    float w = 6.2831853f / (response > 0.02f ? response : 0.02f);
    float k = w * w, c = 2.f * damping * w;
    int n = (int)(dt / 0.004f) + 1;                               // pasos de 4 ms: estable con cualquier response
    float h = dt / (float)n;
    for (int i = 0; i < n; i++) { v += (k * (target - x) - c * v) * h; x += v * h; }
    if (fabsf(target - x) < 0.01f && fabsf(v) < 0.02f) { x = target; v = 0.f; }
}

// ---------------------------------------------------------------------------------------------
enum Kind : u8 { K_RRECT, K_STROKE, K_SHADOW, K_DISC, K_FADELINE };
struct Shape { u8 kind, alpha, r, blur, stat, page; s16 x, y, w, h; float fx, fy, fr; Rgb c0, c1; };   // stat: fija (caché); page: 1 = de la página (se desvanece), 2 = además se desplaza
// v17: japonés/coreano = 3 bytes por letra. v17.0 r2: 'game' = va con la fuente del juego (se decide al añadirlo y
// entra en su huella: si cambia, se repinta; antes un texto pintado una vez con la fuente equivocada se quedaba con "?")
struct TextItem { s16 x, y; Rgb c; bool small, game; u8 page; char s[112]; };

static const int MAX_SHAPES = 700, MAX_TEXTS = 110, STRIP_H = 48;
static Shape    s_shape[MAX_SHAPES];
static int      s_ns = 0;
static TextItem s_text[MAX_TEXTS];
static int      s_nt = 0;
static bool     s_top = true;
static int      s_W = 400;
static bool     s_static = false;                                 // las formas nuevas son "fijas" (paneles de cristal)
static int      s_crX = 0, s_crY = 0, s_crW = 0, s_crH = 0;       // rectángulo del caché de paneles (este cuadro)
static inline u32 Fnv(u32 h, const void *p, size_t n);
void StaticBegin() { s_static = true; }
void StaticEnd() { s_static = false; }
// Página que entra (v16.2): ver UiGfx.hpp
static float    s_pg = 1.f;
static int      s_pdx = 0, s_pdy = 0;
static Rgb      s_ptext = { 0, 0, 0 };
static u8       s_pageMode = 0;
void PageParams(float pg, int dx, int dy, Rgb textBase) {
    s_pg = pg < 0.f ? 0.f : pg > 1.f ? 1.f : pg;
    s_pdx = dx; s_pdy = dy; s_ptext = textBase;
}
void PageBegin(bool move) { s_pageMode = move ? 2 : 1; }
void PageEnd() { s_pageMode = 0; }
// Aparición del menú (v16.2): ver UiGfx.hpp
static float    s_open = 1.f;
static int      s_ody = 0;
static Rgb      s_otext = { 0, 0, 0 };
void OpenParams(float open, int dy, Rgb textBase) {
    s_open = open < 0.f ? 0.f : open > 1.f ? 1.f : open;
    s_ody = dy; s_otext = textBase;
}
static u16      s_strip[400 * STRIP_H];                           // lienzo de una franja (RGB565)

// Fondo desenfocado a 1/4 de resolución (RGB888)
static const int LH = 60;
static u8   s_bg[2][100 * LH * 3];
static bool s_bgOk[2] = { false, false };
static u16 *s_bgFull[2] = { nullptr, nullptr };                   // el fondo ya calculado a tamaño real (si hay memoria)
static bool s_bgTried[2] = { false, false };                      // se intenta DESPUÉS del caché de paneles (vale más)
static void Prebake(int idx);

static const u8 kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

// Trama ordenada (sin escalones en 16 bits). Tablas: s_q5[d][v + 8] = canal de 5 bits ya tramado y recortado,
// s_q6 igual para el verde (6 bits); v en -8..263. Mismo resultado que la cuenta directa, menos operaciones.
static u8   s_q5[16][272], s_q6[16][272];
static bool s_qInit = false;
static void InitQ() {
    for (int d = 0; d < 16; d++)
        for (int i = 0; i < 272; i++) {
            int v = i - 8;
            int r = v + (d >> 1) - 4, g = v + (d >> 2) - 2;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            s_q5[d][i] = (u8)(r >> 3); s_q6[d][i] = (u8)(g >> 2);
        }
    s_qInit = true;
}
static inline u16 Pack(int r, int g, int b, int x, int y) {
    int d = kBayer[y & 3][x & 3];
    return (u16)((s_q5[d][r + 8] << 11) | (s_q6[d][g + 8] << 5) | s_q5[d][b + 8]);
}

// a = 0..256
static inline void Blend(u16 *p, int x, int y, Rgb c, int a) {
    if (a <= 0) return;
    if (a >= 256) { *p = Pack(c.r, c.g, c.b, x, y); return; }
    u16 d = *p;
    int r5 = d >> 11, g6 = (d >> 5) & 63, b5 = d & 31;
    int dr = (r5 << 3) | (r5 >> 2), dg = (g6 << 2) | (g6 >> 4), db = (b5 << 3) | (b5 >> 2);
    *p = Pack(dr + (((c.r - dr) * a) >> 8), dg + (((c.g - dg) * a) >> 8), db + (((c.b - db) * a) >> 8), x, y);
}

static inline float Clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

// Distancia con signo a un rectángulo redondeado (negativa dentro), en el centro del píxel (px, py)
static inline float RRDist(const Shape &s, float px, float py) {
    float hx = s.w * 0.5f, hy = s.h * 0.5f;
    float r = (float)s.r;
    if (r > hx) r = hx;
    if (r > hy) r = hy;
    float qx = fabsf(px - (s.x + hx)) - (hx - r), qy = fabsf(py - (s.y + hy)) - (hy - r);
    float ox = qx > 0.f ? qx : 0.f, oy = qy > 0.f ? qy : 0.f;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0.f ? in : 0.f) - r;
}

// ---------------------------------------------------------------------------------------------
static void CheckFontGen();
void Begin(bool top) {
    if (!s_qInit) InitQ();
    CheckFontGen();
    s_static = false;
    s_crW = 0;
    s_pg = 1.f; s_pdx = s_pdy = 0; s_pageMode = 0;
    s_open = 1.f; s_ody = 0;
    s_top = top;
    s_W = top ? 400 : 320;
    s_ns = 0;
    s_nt = 0;
}
int Width() { return s_W; }

static Shape *NewShape(Kind k, int x, int y, int w, int h, int r, Rgb c0, Rgb c1, int alpha) {
    if (s_ns >= MAX_SHAPES || alpha <= 0 || w <= 0 || h <= 0) return nullptr;
    Shape &s = s_shape[s_ns++];
    s.kind = k; s.alpha = (u8)(alpha > 255 ? 255 : alpha);
    s.r = (u8)(r < 0 ? 0 : r > 60 ? 60 : r);
    s.blur = 0;
    s.stat = s_static ? 1 : 0;
    s.page = s_pageMode;
    s.x = (s16)x; s.y = (s16)y; s.w = (s16)w; s.h = (s16)h;
    s.fx = s.fy = s.fr = 0.f;
    s.c0 = c0; s.c1 = c1;
    return &s;
}

void RRect(int x, int y, int w, int h, int r, Rgb c, int alpha) { NewShape(K_RRECT, x, y, w, h, r, c, c, alpha); }
void RRectV(int x, int y, int w, int h, int r, Rgb top, Rgb bottom, int alpha) { NewShape(K_RRECT, x, y, w, h, r, top, bottom, alpha); }
void Stroke(int x, int y, int w, int h, int r, Rgb c, int alpha) { NewShape(K_STROKE, x, y, w, h, r, c, c, alpha); }
void Shadow(int x, int y, int w, int h, int r, int blur, int alpha) {
    Shape *s = NewShape(K_SHADOW, x, y, w, h, r, RGB(0, 0, 8), RGB(0, 0, 8), alpha);
    if (s) s->blur = (u8)(blur < 1 ? 1 : blur > 30 ? 30 : blur);
}
void Disc(float cx, float cy, float r, Rgb c, int alpha) {
    Shape *s = NewShape(K_DISC, (int)floorf(cx - r - 1), (int)floorf(cy - r - 1), (int)(2 * r + 3), (int)(2 * r + 3), 0, c, c, alpha);
    if (s) { s->fx = cx; s->fy = cy; s->fr = r; }
}
void DiscTop(float cx, float cy, float r, Rgb c, int alpha) {
    int y0 = (int)floorf(cy - r - 1);
    Shape *s = NewShape(K_DISC, (int)floorf(cx - r - 1), y0, (int)(2 * r + 3), (int)floorf(cy) - y0, 0, c, c, alpha);   // filas hasta el centro
    if (s) { s->fx = cx; s->fy = cy; s->fr = r; }
}
void FadeLine(int x, int y, int w, Rgb c, int alpha) { NewShape(K_FADELINE, x, y, w, 1, 0, c, c, alpha); }

static bool UseGameFont(const char *t);
static void AddText(const char *t, int x, int y, Rgb c, bool small) {
    if (!t || !t[0] || s_nt >= MAX_TEXTS) return;
    TextItem &it = s_text[s_nt++];
    it.x = (s16)x; it.y = (s16)y; it.c = c; it.small = small; it.page = s_pageMode;
    snprintf(it.s, sizeof(it.s), "%s", t);
    size_t n = strlen(it.s);                                     // si se cortó, no dejar media letra UTF-8
    if (n == sizeof(it.s) - 1) { while (n > 0 && ((u8)it.s[n - 1] & 0xC0) == 0x80) n--; if (n > 0 && (u8)it.s[n - 1] >= 0xC0) n--; it.s[n] = 0; }
    it.game = !small && UseGameFont(it.s);
}
// Medir texto con la fuente del sistema es caro (busca cada letra en la tabla de la fuente) y Fit() lo hace
// muchas veces: se guardan los anchos ya medidos (los textos del menú se repiten en cada cuadro).
struct WEntry { u32 hash; u16 len; s16 w; };
static WEntry s_wcache[512];
static u32 s_wgen = 0;                     // v17: al cambiar la fuente de japonés/coreano se olvidan los anchos
static bool HighBytes(const char *t) { for (; t && *t; t++) if ((u8)*t >= 0x80) return true; return false; }
// v17: en japonés/coreano los textos con letras no latinas van con la fuente del juego (GameFont.hpp)
static bool UseGameFont(const char *t) { return HighBytes(t) && Lang::Cjk() && GameFont::Active(); }
static void CheckFontGen() {                     // cambió la fuente: medir y pintar todo de nuevo
    if (GameFont::Generation() == s_wgen) return;
    s_wgen = GameFont::Generation();
    memset(s_wcache, 0, sizeof(s_wcache));
    Invalidate();
}
Prof g_prof;
int  TextW(const char *t) {
    if (!t || !t[0]) return 0;
    u32 hsh = 2166136261u;
    size_t n = 0;
    for (const char *p = t; *p; p++, n++) { hsh ^= (u8)*p; hsh *= 16777619u; }
    WEntry &e = s_wcache[hsh & 511];
    if (e.hash == hsh && e.len == (u16)n && e.len) { g_prof.wHit++; return e.w; }
    u64 t0 = svcGetSystemTick();
    int w = UseGameFont(t) ? GameFont::Width(t) : (int)OSD::GetTextWidth(true, t);
    g_prof.wTicks += svcGetSystemTick() - t0;
    g_prof.wMiss++;
    e.hash = hsh; e.len = (u16)n; e.w = (s16)w;
    return w;
}
// v17: la letra pequeña (6 px) solo tiene letras latinas: en japonés/coreano esos textos van con la del sistema
static bool SmallAsSys(const char *t) { return Lang::Cjk() && HighBytes(t); }
int  SmallW(const char *t) {
    if (SmallAsSys(t)) return TextW(t);
    char a[64]; App::AsciiFold(t, a, sizeof(a)); return (int)strlen(a) * 6;
}
void Text(const char *t, int x, int y, Rgb c) { AddText(t, x, y, c, false); }
void TextRight(const char *t, int xr, int y, Rgb c) { AddText(t, xr - TextW(t), y, c, false); }
void TextCenter(const char *t, int xc, int y, Rgb c) { AddText(t, xc - TextW(t) / 2, y, c, false); }
void Small(const char *t, int x, int y, Rgb c) {
    if (SmallAsSys(t)) { AddText(t, x, y - 3, c, false); return; }
    char a[64]; App::AsciiFold(t, a, sizeof(a)); AddText(a, x, y, c, true);
}
void SmallCenter(const char *t, int xc, int y, Rgb c) { Small(t, xc - SmallW(t) / 2, y, c); }

void Fit(const char *in, int maxW, char *out, size_t cap) {
    snprintf(out, cap, "%s", in ? in : "");
    if (TextW(out) <= maxW) return;
    size_t n = strlen(out);
    while (n > 0) {
        do { n--; } while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);   // no partir una letra UTF-8
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%.*s...", (int)n, out);
        if (TextW(tmp) <= maxW || n == 0) { snprintf(out, cap, "%s", tmp); return; }
    }
}


// ---------------------------------------------------------------------------------------------
// Destino del rasterizado: filas [y0, y1) y columnas [x0, x1) de una imagen de ancho s_W cuya fila dstY0 está en dst
struct Dst { u16 *buf; int stride, dstX0, dstY0, y0, y1, x0, x1; };   // row[x] = píxel (x, y) de pantalla
static inline u16 *Row(const Dst &d, int y) { return d.buf + (y - d.dstY0) * d.stride - d.dstX0; }

static void Raster(const Shape &s, const Dst &d) {
    int a8 = s.alpha + (s.alpha >> 7);                              // 0..256
    const int y0 = d.y0, y1 = d.y1, cx0 = d.x0, cx1 = d.x1;
    switch (s.kind) {
        case K_RRECT: {
            int ya = s.y > y0 ? s.y : y0, yb = s.y + s.h < y1 ? s.y + s.h : y1;
            int xa = s.x > cx0 ? s.x : cx0, xb = s.x + s.w < cx1 ? s.x + s.w : cx1;
            if (ya >= yb || xa >= xb) return;
            int r = s.r;
            if (r * 2 > s.w) r = s.w / 2;
            if (r * 2 > s.h) r = s.h / 2;
            bool grad = memcmp(&s.c0, &s.c1, sizeof(Rgb)) != 0;
            for (int y = ya; y < yb; y++) {
                Rgb c = grad ? Mix(s.c0, s.c1, s.h > 1 ? (float)(y - s.y) / (float)(s.h - 1) : 0.f) : s.c0;
                u16 *row = Row(d, y);
                bool corner = y < s.y + r || y >= s.y + s.h - r;
                if (!corner) { for (int x = xa; x < xb; x++) Blend(row + x, x, y, c, a8); continue; }
                int edgeL = s.x + r + 1, edgeR = s.x + s.w - r - 1;      // fuera de [edgeL, edgeR): esquina (con borde suave)
                for (int x = xa; x < xb; x++) {
                    if (x >= edgeL && x < edgeR) { Blend(row + x, x, y, c, a8); continue; }
                    float cov = Clamp01(0.5f - RRDist(s, x + 0.5f, y + 0.5f));
                    if (cov > 0.f) Blend(row + x, x, y, c, (int)(cov * a8));
                }
            }
            break;
        }
        case K_STROKE: {
            int ya = s.y > y0 ? s.y : y0, yb = s.y + s.h < y1 ? s.y + s.h : y1;
            int xa = s.x > cx0 ? s.x : cx0, xb = s.x + s.w < cx1 ? s.x + s.w : cx1;
            if (ya >= yb || xa >= xb) return;
            int r = s.r;
            for (int y = ya; y < yb; y++) {
                u16 *row = Row(d, y);
                bool band = y <= s.y + r + 1 || y >= s.y + s.h - r - 2;
                for (int x = xa; x < xb; x++) {
                    if (!band && x > s.x + 1 && x < s.x + s.w - 2) { x = s.x + s.w - 3; continue; }   // interior: nada
                    float dd = RRDist(s, x + 0.5f, y + 0.5f);
                    float cov = Clamp01(0.5f - dd) - Clamp01(-0.5f - dd);  // 1 px por dentro del borde
                    if (cov > 0.f) Blend(row + x, x, y, s.c0, (int)(cov * a8));
                }
            }
            break;
        }
        case K_SHADOW: {
            int B = s.blur;
            int sx = s.x - B, sy = s.y - B + B / 3, sw = s.w + 2 * B, sh = s.h + 2 * B;   // un poco hacia abajo (luz arriba)
            int ya = sy > y0 ? sy : y0, yb = sy + sh < y1 ? sy + sh : y1;
            int xa = sx > cx0 ? sx : cx0, xb = sx + sw < cx1 ? sx + sw : cx1;
            if (ya >= yb || xa >= xb) return;
            Shape body = s;
            body.y = (s16)(s.y + B / 3);
            for (int y = ya; y < yb; y++) {
                u16 *row = Row(d, y);
                bool inner = y >= body.y + body.r && y < body.y + body.h - body.r;
                for (int x = xa; x < xb; x++) {
                    if (inner && x >= body.x + 1 && x < body.x + body.w - 1) { x = body.x + body.w - 2; continue; }   // debajo del panel: nada
                    float dd = RRDist(body, x + 0.5f, y + 0.5f);
                    if (dd < -1.f) continue;
                    float t = 1.f - Clamp01(dd / (float)B);
                    int a = (int)(t * t * a8);
                    if (a > 0) Blend(row + x, x, y, s.c0, a);
                }
            }
            break;
        }
        case K_DISC: {
            int ya = s.y > y0 ? s.y : y0, yb = s.y + s.h < y1 ? s.y + s.h : y1;
            int xa = s.x > cx0 ? s.x : cx0, xb = s.x + s.w < cx1 ? s.x + s.w : cx1;
            for (int y = ya; y < yb; y++) {
                u16 *row = Row(d, y);
                float dy = y + 0.5f - s.fy;
                for (int x = xa; x < xb; x++) {
                    float dx = x + 0.5f - s.fx;
                    float cov = Clamp01(s.fr + 0.5f - sqrtf(dx * dx + dy * dy));
                    if (cov > 0.f) Blend(row + x, x, y, s.c0, (int)(cov * a8));
                }
            }
            break;
        }
        case K_FADELINE: {
            if (s.y < y0 || s.y >= y1 || s.y < 0 || s.y >= 240) return;
            u16 *row = Row(d, s.y);
            int ramp = s.w / 5 > 1 ? s.w / 5 : 1;
            for (int i = 0; i < s.w; i++) {
                int x = s.x + i;
                if (x < cx0 || x >= cx1) continue;
                int e = i < s.w - 1 - i ? i : s.w - 1 - i;
                int a = e >= ramp ? a8 : a8 * e / ramp;
                Blend(row + x, x, s.y, s.c0, a);
            }
            break;
        }
    }
}

// Rectángulo que ocupa una forma en pantalla (con su sombra)
static inline void ShapeBox(const Shape &s, int &x0, int &y0, int &x1, int &y1) {
    if (s.kind == K_SHADOW) { x0 = s.x - s.blur; y0 = s.y - s.blur; x1 = s.x + s.w + s.blur; y1 = s.y + s.h + 2 * s.blur; }
    else { x0 = s.x; y0 = s.y; x1 = s.x + s.w; y1 = s.y + s.h; }
}

// Fondo: el juego desenfocado (bilineal desde 1/4) o un degradado azul marino con un brillo suave
static void ComputeBg(const Dst &d) {
    const int W = s_W, LW = W / 4;
    const int idx = s_top ? 0 : 1;
    if (s_bgOk[idx]) {
        const u8 *bg = s_bg[idx];
        static int vrow[100 * 3];                                  // la fila de 1/4 ya mezclada en vertical (una vez por fila)
        for (int y = d.y0; y < d.y1; y++) {
            u16 *row = Row(d, y);
            int fy = ((y * 2 + 1) * 128) / 4 - 128;                   // (y + 0.5) / 4 - 0.5, en 1/256
            int iy = fy >> 8, wy = fy & 255;
            int iy0 = iy < 0 ? 0 : iy >= LH ? LH - 1 : iy, iy1 = iy + 1 < 0 ? 0 : iy + 1 >= LH ? LH - 1 : iy + 1;
            const u8 *r0 = bg + iy0 * LW * 3, *r1 = bg + iy1 * LW * 3;
            int lx0 = (d.x0 >> 2) - 1, lx1 = (d.x1 >> 2) + 1;           // solo las columnas de 1/4 que hacen falta
            if (lx0 < 0) lx0 = 0;
            if (lx1 > LW - 1) lx1 = LW - 1;
            for (int i = lx0 * 3; i <= lx1 * 3 + 2; i++) vrow[i] = (r0[i] * (256 - wy) + r1[i] * wy) >> 8;
            for (int x = d.x0; x < d.x1; x++) {
                int fx = ((x * 2 + 1) * 128) / 4 - 128;
                int ix = fx >> 8, wx = fx & 255;
                int ix0 = ix < 0 ? 0 : ix >= LW ? LW - 1 : ix, ix1 = ix + 1 < 0 ? 0 : ix + 1 >= LW ? LW - 1 : ix + 1;
                const int *p0 = vrow + ix0 * 3, *p1 = vrow + ix1 * 3;
                row[x] = Pack((p0[0] * (256 - wx) + p1[0] * wx) >> 8, (p0[1] * (256 - wx) + p1[1] * wx) >> 8,
                              (p0[2] * (256 - wx) + p1[2] * wx) >> 8, x, y);
            }
        }
        return;
    }
    for (int y = d.y0; y < d.y1; y++) {
        u16 *row = Row(d, y);
        float t = y / 239.f;
        int br = (int)(10 + 12 * t), bgc = (int)(16 + 18 * t), bb = (int)(36 + 34 * t);
        for (int x = d.x0; x < d.x1; x++) {
            float dx = (x - W * 0.3f) / (W * 0.8f), dy = (y - 30.f) / 240.f;
            float g = 1.f - (dx * dx + dy * dy) * 2.2f;
            int add = g > 0.f ? (int)(g * g * 26.f) : 0;           // brillo azul arriba a la izquierda
            row[x] = Pack(br + add / 3, bgc + add / 2, bb + add, x, y);
        }
    }
}

static void FillBg(const Dst &d) {
    const u16 *full = s_bgFull[s_top ? 0 : 1];
    if (full) {
        for (int y = d.y0; y < d.y1; y++) memcpy(Row(d, y) + d.x0, full + y * s_W + d.x0, (size_t)(d.x1 - d.x0) * sizeof(u16));
    } else ComputeBg(d);
}

// v16.2: filtro de la composición en el modo "página guardada": solo las formas que NO son de la página y van
// antes de la primera de ella (lo de encima se pinta después de mezclar la página)
static bool s_baseOnly = false;
static int  s_firstPage = 0;
static inline bool Skip(int i) { return s_baseOnly && (s_shape[i].page || i >= s_firstPage); }

// Compone una región: fondo + todas las formas que la tocan, en orden
static void ComposeAll(const Dst &d) {
    FillBg(d);
    for (int i = 0; i < s_ns; i++) {
        if (Skip(i)) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (by1 <= d.y0 || by0 >= d.y1 || bx1 <= d.x0 || bx0 >= d.x1) continue;
        Raster(s_shape[i], d);
    }
}

// ---- Caché de paneles fijos (v16.1). Los paneles de cristal (fondo desenfocado + cristal + sombra + borde) son lo más
// caro de componer y casi nunca cambian. Dentro del rectángulo pedido (el panel principal de cada pantalla) se guarda
// ya compuesto "fondo + formas fijas"; al recomponer solo se copia y se dibujan encima las formas que cambian.
// Solo vale si ninguna forma NO fija que vaya antes (debajo) de una fija toca ese rectángulo: se comprueba cada cuadro.
struct Chrome { u16 *buf; int cap, x, y, w, h; u32 sig; bool ok, tried; const char *why; };
static Chrome s_ch[2];
static bool   s_chUse = false;

// Memoria (~200 KB libres con el menú abierto): primero los dos cachés de paneles (lo que más acelera) y, cuando las
// dos pantallas ya lo intentaron, el fondo precalculado con lo que sobre.
static bool s_chLogged = false;
static void AfterChromeTry() {
    if (!s_ch[0].tried || !s_ch[1].tried || s_chLogged) return;
    s_chLogged = true;
    for (int i = 0; i < 2; i++) if (!s_bgTried[i]) { s_bgTried[i] = true; Prebake(i); }   // v16.2: la aparición ya lo pudo hacer
    Log::Write("menu: caché de paneles arriba %s, abajo %s | fondo listo %s/%s", s_ch[0].buf ? "si" : s_ch[0].why,
               s_ch[1].buf ? "si" : s_ch[1].why, s_bgFull[0] ? "si" : "no", s_bgFull[1] ? "si" : "no");
}                                     // este cuadro se compone con el caché

void ChromeRect(int x, int y, int w, int h) { s_crX = x; s_crY = y; s_crW = w; s_crH = h; }

static void PrepareChrome() {
    s_chUse = false;
    Chrome &C = s_ch[s_top ? 0 : 1];
    if (s_crW <= 0 || s_crH <= 0) return;
    int x0 = s_crX < 0 ? 0 : s_crX, y0 = s_crY < 0 ? 0 : s_crY;
    int x1 = s_crX + s_crW > s_W ? s_W : s_crX + s_crW, y1 = s_crY + s_crH > 240 ? 240 : s_crY + s_crH;
    if (x1 <= x0 || y1 <= y0) return;
    // ¿alguna forma no fija por DEBAJO de una fija toca el rectángulo? entonces no se puede usar
    int lastStatic = -1;
    for (int i = 0; i < s_ns; i++) if (s_shape[i].stat) lastStatic = i;
    if (lastStatic < 0) return;                                   // (primer cuadro del menú: aún todo invisible)
    for (int i = 0; i < lastStatic; i++) {
        if (s_shape[i].stat) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (!(by1 <= y0 || by0 >= y1 || bx1 <= x0 || bx0 >= x1)) { if (!C.tried) { C.tried = true; C.why = "orden"; AfterChromeTry(); } return; }
    }
    int need = (x1 - x0) * (y1 - y0);
    if (!C.buf || C.cap < need) {
        free(C.buf);
        C.buf = (u16 *)malloc((size_t)need * sizeof(u16));
        C.cap = C.buf ? need : 0;
        C.ok = false;
        if (!C.tried) { C.tried = true; C.why = C.buf ? "" : "sin memoria"; AfterChromeTry(); }
        if (!C.buf) return;                                        // sin memoria: se compone como siempre
    }
    u32 sig = 2166136261u;
    int rc[4] = { x0, y0, x1, y1 };
    sig = Fnv(sig, rc, sizeof(rc));
    for (int i = 0; i < s_ns; i++) {
        if (!s_shape[i].stat) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (by1 <= y0 || by0 >= y1 || bx1 <= x0 || bx0 >= x1) continue;
        sig = Fnv(sig, &s_shape[i], sizeof(Shape));
    }
    if (!C.ok || sig != C.sig || C.x != x0 || C.y != y0 || C.w != x1 - x0 || C.h != y1 - y0) {
        C.x = x0; C.y = y0; C.w = x1 - x0; C.h = y1 - y0;
        Dst d = { C.buf, C.w, C.x, C.y, C.y, C.y + C.h, C.x, C.x + C.w };
        FillBg(d);
        for (int i = 0; i < s_ns; i++) {
            if (!s_shape[i].stat) continue;
            int bx0, by0, bx1, by1;
            ShapeBox(s_shape[i], bx0, by0, bx1, by1);
            if (by1 <= d.y0 || by0 >= d.y1 || bx1 <= d.x0 || bx0 >= d.x1) continue;
            Raster(s_shape[i], d);
        }
        C.sig = sig; C.ok = true;
        g_prof.chromeBuilds++;
    }
    s_chUse = true;
}

// Dentro del caché: copiar "fondo + fijas" y dibujar solo las que cambian
static void ComposeInside(const Dst &d, const Chrome &C) {
    for (int y = d.y0; y < d.y1; y++)
        memcpy(Row(d, y) + d.x0, C.buf + (y - C.y) * C.w + (d.x0 - C.x), (size_t)(d.x1 - d.x0) * sizeof(u16));
    for (int i = 0; i < s_ns; i++) {
        if (s_shape[i].stat || Skip(i)) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (by1 <= d.y0 || by0 >= d.y1 || bx1 <= d.x0 || bx0 >= d.x1) continue;
        Raster(s_shape[i], d);
    }
}

static void ComposeRegion(const Dst &d) {
    const Chrome &C = s_ch[s_top ? 0 : 1];
    int iy0 = d.y0 > C.y ? d.y0 : C.y, iy1 = d.y1 < C.y + C.h ? d.y1 : C.y + C.h;
    int ix0 = d.x0 > C.x ? d.x0 : C.x, ix1 = d.x1 < C.x + C.w ? d.x1 : C.x + C.w;
    if (!s_chUse || iy0 >= iy1 || ix0 >= ix1) { ComposeAll(d); return; }
    // la región se parte en: franja de arriba, de abajo, y en medio izquierda / caché / derecha
    Dst p = d;
    if (d.y0 < iy0) { p = d; p.y1 = iy0; ComposeAll(p); }
    if (iy1 < d.y1) { p = d; p.y0 = iy1; ComposeAll(p); }
    if (d.x0 < ix0) { p = d; p.y0 = iy0; p.y1 = iy1; p.x1 = ix0; ComposeAll(p); }
    if (ix1 < d.x1) { p = d; p.y0 = iy0; p.y1 = iy1; p.x0 = ix1; ComposeAll(p); }
    p = d; p.y0 = iy0; p.y1 = iy1; p.x0 = ix0; p.x1 = ix1;
    ComposeInside(p, C);
}

// ---------------------------------------------------------------------------------------------
// v16.2: PÁGINA GUARDADA. Al cambiar de sección, el contenido nuevo entra desvaneciéndose y desplazándose unos
// píxeles durante ~0,3 s: antes TODAS sus formas se volvían a rasterizar en cada cuadro de la transición (~22 ms
// por cuadro con las dos pantallas: 27 vueltas/s mientras navegas). Ahora la página se compone UNA vez en su estado
// final (F) y en cada cuadro solo se mezcla con lo que hay debajo: lerp(base, F desplazada, pg), unas pocas
// operaciones por píxel. Se ve igual (las formas de la página son translúcidas sobre un panel de cristal casi
// uniforme). Sin memoria para F, o si la página cambia en cada cuadro, se dibuja como antes (exacto).
struct Snap { u16 *buf; int cap, x, y, w, h; u32 key; bool valid; u32 lastBuild; int built, restarts; };
static Snap s_snap[2];
static u32  s_frameOf[2] = { 0, 0 };                              // cuadros pintados de cada pantalla

// a32: 0..32. Mezcla de dos píxeles RGB565 con los tres canales a la vez (campos separados en 32 bits)
static inline u16 Lerp565(u32 b, u32 f, u32 a32) {
    u32 B = (b | (b << 16)) & 0x07E0F81Fu, F = (f | (f << 16)) & 0x07E0F81Fu;
    u32 R = ((B * (32 - a32) + F * a32) >> 5) & 0x07E0F81Fu;
    return (u16)(R | (R >> 16));
}

// Prepara F (si hace falta) con la lista en su estado FINAL. Devuelve true si este cuadro puede usarla.
static bool BuildSnap() {
    s_firstPage = -1;
    int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;
    for (int i = 0; i < s_ns; i++) {
        if (!s_shape[i].page) continue;
        if (s_firstPage < 0) s_firstPage = i;
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (bx0 < x0) x0 = bx0;
        if (by0 < y0) y0 = by0;
        if (bx1 > x1) x1 = bx1;
        if (by1 > y1) y1 = by1;
    }
    if (s_firstPage < 0) return false;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_W) x1 = s_W;
    if (y1 > 240) y1 = 240;
    if (x1 <= x0 || y1 <= y0) return false;
    Snap &S = s_snap[s_top ? 0 : 1];
    // huella: el rectángulo, las formas de debajo que lo tocan y las de la página
    u32 key = 2166136261u;
    int rc[4] = { x0, y0, x1, y1 };
    key = Fnv(key, rc, sizeof(rc));
    for (int i = 0; i < s_ns; i++) {
        const Shape &sh = s_shape[i];
        if (!sh.page) {
            if (i >= s_firstPage) continue;
            int bx0, by0, bx1, by1;
            ShapeBox(sh, bx0, by0, bx1, by1);
            if (by1 <= y0 || by0 >= y1 || bx1 <= x0 || bx0 >= x1) continue;
        }
        key = Fnv(key, &sh, sizeof(Shape));
    }
    const u32 frame = s_frameOf[s_top ? 0 : 1];
    if (S.valid && key == S.key) return true;
    if (S.lastBuild && S.lastBuild + 1 == frame) { S.key = key; S.valid = false; S.lastBuild = frame; return false; }   // cambia sin parar
    int need = (x1 - x0) * (y1 - y0);
    if (!S.buf || S.cap < need) {
        free(S.buf);
        S.buf = (u16 *)malloc((size_t)need * sizeof(u16));
        S.cap = S.buf ? need : 0;
        if (!S.buf) { S.valid = false; return false; }               // sin memoria: como antes
    }
    S.x = x0; S.y = y0; S.w = x1 - x0; S.h = y1 - y0;
    Dst d = { S.buf, S.w, S.x, S.y, S.y, S.y + S.h, S.x, S.x + S.w };
    FillBg(d);
    for (int i = 0; i < s_ns; i++) {                                // debajo + la página, en su orden
        const Shape &sh = s_shape[i];
        if (!sh.page && i >= s_firstPage) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(sh, bx0, by0, bx1, by1);
        if (by1 <= d.y0 || by0 >= d.y1 || bx1 <= d.x0 || bx0 >= d.x1) continue;
        Raster(sh, d);
    }
    S.key = key; S.valid = true; S.lastBuild = frame;
    g_prof.pageBuilds++;
    return true;
}

// Aplica la transición a la lista (opacidad x pg y desplazamiento): así la comparación por zonas, el texto y el
// modo exacto ven exactamente lo que se ve en pantalla.
static void TransformPage() {
    int a256 = (int)(s_pg * 256.f + 0.5f);
    for (int i = 0; i < s_ns; i++) {
        Shape &sh = s_shape[i];
        if (!sh.page) continue;
        sh.alpha = (u8)((sh.alpha * a256 + 128) >> 8);
        if (sh.page == 2) { sh.x = (s16)(sh.x + s_pdx); sh.y = (s16)(sh.y + s_pdy); sh.fx += s_pdx; sh.fy += s_pdy; }
    }
    for (int i = 0; i < s_nt; i++) {
        TextItem &it = s_text[i];
        if (!it.page) continue;
        it.c = Mix(s_ptext, it.c, s_pg);
        if (it.page == 2) { it.x = (s16)(it.x + s_pdx); it.y = (s16)(it.y + s_pdy); }
    }
}

// ---- Aparición del menú con la pantalla guardada (v16.2) ----
// Al abrir, TODO aparece (opacidad 0 -> 1) y sube 10 px durante ~0,4 s. Antes cada cuadro de la aparición volvía a
// componer las dos pantallas enteras (los paneles de cristal cambiaban de opacidad: ni el caché de paneles servía):
// unos pocos cuadros por segundo justo al abrir. Ahora la pantalla final se compone UNA vez (F, pantalla completa) y
// cada cuadro es: fondo desenfocado + mezcla con F desplazada. Al terminar se suelta F (deja sitio a los cachés).
// Devuelve 2 = F lista, 1 = construyéndose (este cuadro solo el fondo: el menú aún es casi transparente), 0 = no se
// puede (sin memoria o la pantalla cambia sin parar): como antes. Se construye por partes para no congelar el primer
// cuadro: primero el fondo desenfocado a tamaño real (si no estaba) y luego F en 3 franjas de 80 filas.
static int BuildFull() {
    const int idx = s_top ? 0 : 1;
    Snap &S = s_snap[idx];
    u32 key = 2166136261u ^ 0x5A5Au;
    key = Fnv(key, &s_W, sizeof(s_W));
    key = Fnv(key, s_shape, (size_t)s_ns * sizeof(Shape));
    const bool same = key == S.key && S.x == 0 && S.y == 0 && S.w == s_W && S.h == 240;
    if (S.valid && same) return 2;
    if (!same) {                                                      // empezar (o volver a empezar) a construir
        if (S.restarts > 6) return 0;
        S.restarts++;
        int need = s_W * 240;
        if (!S.buf || S.cap < need) {
            free(S.buf);
            S.buf = (u16 *)malloc((size_t)need * sizeof(u16));
            S.cap = S.buf ? need : 0;
            if (!S.buf) { S.valid = false; S.restarts = 99; return 0; }   // sin memoria: como antes
        }
        S.x = 0; S.y = 0; S.w = s_W; S.h = 240; S.key = key; S.valid = false;
        S.built = (!s_bgFull[idx] && !s_bgTried[idx]) ? -1 : 0;       // -1: primero el fondo a tamaño real
        if (S.built < 0) return 1;
    }
    if (S.built < 0) {
        s_bgTried[idx] = true;
        Prebake(idx);
        S.built = 0;
        return 1;
    }
    int y0 = S.built, y1 = y0 + 80 > 240 ? 240 : y0 + 80;
    Dst d = { S.buf, s_W, 0, 0, y0, y1, 0, s_W };
    FillBg(d);
    for (int i = 0; i < s_ns; i++) {
        int bx0, by0, bx1, by1;
        ShapeBox(s_shape[i], bx0, by0, bx1, by1);
        if (by1 <= y0 || by0 >= y1) continue;
        Raster(s_shape[i], d);
    }
    S.built = y1;
    if (S.built >= 240) { S.valid = true; g_prof.pageBuilds++; return 2; }
    return 1;
}

static void TransformAll() {
    int a256 = (int)(s_open * 256.f + 0.5f);
    for (int i = 0; i < s_ns; i++) {
        Shape &sh = s_shape[i];
        sh.alpha = (u8)((sh.alpha * a256 + 128) >> 8);
        sh.y = (s16)(sh.y + s_ody); sh.fy += s_ody;
    }
    for (int i = 0; i < s_nt; i++) {
        TextItem &it = s_text[i];
        it.c = Mix(s_otext, it.c, s_open);
        it.y = (s16)(it.y + s_ody);
    }
}

static int s_fullState = 0;
static void ComposeFull(const Dst &d) {
    FillBg(d);
    if (s_fullState != 2) return;                                    // construyéndose: solo el fondo
    const Snap &S = s_snap[s_top ? 0 : 1];
    u32 a32 = (u32)(s_open * 32.f + 0.5f);
    for (int y = d.y0; y < d.y1; y++) {
        int fy = y - s_ody;
        if (fy < 0 || fy >= 240) continue;
        u16 *row = Row(d, y);
        const u16 *frow = S.buf + fy * S.w;
        for (int x = d.x0; x < d.x1; x++) row[x] = Lerp565(row[x], frow[x], a32);
    }
}

// Composición con la página guardada: base (fondo/paneles + formas de debajo), mezcla con F, y lo de encima
static void ComposeSnap(const Dst &d) {
    s_baseOnly = true;
    ComposeRegion(d);
    s_baseOnly = false;
    const Snap &S = s_snap[s_top ? 0 : 1];
    u32 a32 = (u32)(s_pg * 32.f + 0.5f);
    for (int y = d.y0; y < d.y1; y++) {
        int fy = y - s_pdy;
        if (fy < S.y || fy >= S.y + S.h) continue;
        u16 *row = Row(d, y);
        const u16 *frow = S.buf + (fy - S.y) * S.w - S.x;
        int xa = d.x0, xb = d.x1;
        if (xa < S.x + s_pdx) xa = S.x + s_pdx;
        if (xb > S.x + S.w + s_pdx) xb = S.x + S.w + s_pdx;
        for (int x = xa; x < xb; x++) row[x] = Lerp565(row[x], frow[x - s_pdx], a32);
    }
    for (int i = s_firstPage + 1; i < s_ns; i++) {                  // lo que no es de la página y va encima
        const Shape &sh = s_shape[i];
        if (sh.page) continue;
        int bx0, by0, bx1, by1;
        ShapeBox(sh, bx0, by0, bx1, by1);
        if (by1 <= d.y0 || by0 >= d.y1 || bx1 <= d.x0 || bx0 >= d.x1) continue;
        Raster(sh, d);
    }
}

// ---------------------------------------------------------------------------------------------
// Rendimiento (v16.1, medido en vivo con el ARM11 emulado: componer una pantalla entera cuesta ~100 ms).
//  1) Firma por búfer de pantalla: si lo que toca dibujar es IGUAL a lo que ya tiene ese búfer, no se toca nada
//     (el menú quieto se sigue presentando a ~60 cuadros por segundo sin trabajo).
//  2) Zonas sucias: de cada búfer se guarda una "huella" de cada forma y cada texto que muestra (12 bytes por
//     pieza, 40 KB fijos en total: no hace falta guardar la imagen). Al dibujar se compara con la lista nueva y
//     solo se recomponen y copian los cuadros de 8x8 donde algo difiere (p. ej. la barra de selección al bajar).
//  3) Copia a la pantalla en palabras de 32 bits por columna; y solo se vuelven a escribir los textos que tocan
//     esas zonas (escribir un texto dos veces encima lo engordaría: la fuente del sistema mezcla sus bordes).
static const int TILE = 8, TW_MAX = 400 / TILE, TH = 240 / TILE;
struct Key { u32 hash; s16 x0, y0, x1, y1; };                      // huella + caja en pantalla
struct BufState { u8 *base; u32 sig, use; bool full; int ns, nt; u32 extra; int ax, ay, aw, ah; Key shapes[MAX_SHAPES]; Key texts[MAX_TEXTS]; };
static BufState s_buf[4];
static u32      s_bufUse = 0;
static Key      s_curS[MAX_SHAPES], s_curT[MAX_TEXTS];
static int      s_afterX = 0, s_afterY = 0, s_afterW = 0, s_afterH = 0;

void SetAfterRect(int x, int y, int w, int h) { s_afterX = x; s_afterY = y; s_afterW = w; s_afterH = h; }

void Invalidate() {
    for (auto &b : s_buf) { b.base = nullptr; b.sig = 0; b.use = 0; b.full = true; }
    for (auto &C : s_ch) C.ok = false;
}

static inline u32 Fnv(u32 h, const void *p, size_t n) {
    const u8 *b = (const u8 *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static inline u32 TextHash(const TextItem &it) {
    u32 h = 2166136261u;
    h = Fnv(h, &it.x, sizeof(it.x)); h = Fnv(h, &it.y, sizeof(it.y));
    h = Fnv(h, &it.c, sizeof(it.c)); h = Fnv(h, &it.small, sizeof(it.small)); h = Fnv(h, &it.game, sizeof(it.game));
    return Fnv(h, it.s, strlen(it.s));
}
static u32 ListSig(u32 extra) {
    u32 h = 2166136261u;
    h = Fnv(h, &s_top, sizeof(s_top));
    h = Fnv(h, &extra, sizeof(extra));
    h = Fnv(h, s_shape, (size_t)s_ns * sizeof(Shape));      // el relleno de la estructura nunca se escribe: siempre 0
    for (int i = 0; i < s_nt; i++) { u32 th = TextHash(s_text[i]); h = Fnv(h, &th, sizeof(th)); }
    return h ? h : 1u;
}

// ---- mapas de cuadros sucios
static u8 s_dirty[TH][TW_MAX];
static inline int TilesW() { return s_W / TILE; }
static void MarkRect(int x0, int y0, int x1, int y1) {
    int tw = TilesW();
    if (x1 <= 0 || y1 <= 0 || x0 >= s_W || y0 >= 240 || x1 <= x0 || y1 <= y0) return;
    int tx0 = (x0 < 0 ? 0 : x0) / TILE, ty0 = (y0 < 0 ? 0 : y0) / TILE;
    int tx1 = ((x1 > s_W ? s_W : x1) + TILE - 1) / TILE, ty1 = ((y1 > 240 ? 240 : y1) + TILE - 1) / TILE;
    if (tx1 > tw) tx1 = tw;
    if (ty1 > TH) ty1 = TH;
    for (int ty = ty0; ty < ty1; ty++) for (int tx = tx0; tx < tx1; tx++) s_dirty[ty][tx] = 1;
}
static bool AnyRect(int x0, int y0, int x1, int y1) {
    int tw = TilesW();
    if (x1 <= 0 || y1 <= 0 || x0 >= s_W || y0 >= 240 || x1 <= x0 || y1 <= y0) return false;
    int tx0 = (x0 < 0 ? 0 : x0) / TILE, ty0 = (y0 < 0 ? 0 : y0) / TILE;
    int tx1 = ((x1 > s_W ? s_W : x1) + TILE - 1) / TILE, ty1 = ((y1 > 240 ? 240 : y1) + TILE - 1) / TILE;
    if (tx1 > tw) tx1 = tw;
    if (ty1 > TH) ty1 = TH;
    for (int ty = ty0; ty < ty1; ty++) for (int tx = tx0; tx < tx1; tx++) if (s_dirty[ty][tx]) return true;
    return false;
}
// Marca la caja y dice si añadió algún cuadro nuevo
static bool GrowRect(int x0, int y0, int x1, int y1) {
    int tw = TilesW();
    if (x1 <= 0 || y1 <= 0 || x0 >= s_W || y0 >= 240 || x1 <= x0 || y1 <= y0) return false;
    int tx0 = (x0 < 0 ? 0 : x0) / TILE, ty0 = (y0 < 0 ? 0 : y0) / TILE;
    int tx1 = ((x1 > s_W ? s_W : x1) + TILE - 1) / TILE, ty1 = ((y1 > 240 ? 240 : y1) + TILE - 1) / TILE;
    if (tx1 > tw) tx1 = tw;
    if (ty1 > TH) ty1 = TH;
    bool grew = false;
    for (int ty = ty0; ty < ty1; ty++) for (int tx = tx0; tx < tx1; tx++) if (!s_dirty[ty][tx]) { s_dirty[ty][tx] = 1; grew = true; }
    return grew;
}
static inline void TextBox(const TextItem &t, int &x0, int &y0, int &x1, int &y1) {
    int w = t.small ? (int)strlen(t.s) * 6 : TextW(t.s);
    x0 = t.x - 1; y0 = t.y - 1; x1 = t.x + w + 2; y1 = t.y + (t.small ? 11 : 18);
}
static void BuildKeys() {
    for (int i = 0; i < s_ns; i++) {
        int x0, y0, x1, y1;
        ShapeBox(s_shape[i], x0, y0, x1, y1);
        s_curS[i].hash = Fnv(2166136261u, &s_shape[i], sizeof(Shape));
        s_curS[i].x0 = (s16)x0; s_curS[i].y0 = (s16)y0; s_curS[i].x1 = (s16)x1; s_curS[i].y1 = (s16)y1;
    }
    for (int i = 0; i < s_nt; i++) {
        int x0, y0, x1, y1;
        TextBox(s_text[i], x0, y0, x1, y1);
        s_curT[i].hash = TextHash(s_text[i]);
        s_curT[i].x0 = (s16)x0; s_curT[i].y0 = (s16)y0; s_curT[i].x1 = (s16)x1; s_curT[i].y1 = (s16)y1;
    }
}
static inline bool SameKey(const Key &a, const Key &b) { return a.hash == b.hash && a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }
// Diferencias por posición en la lista: se ensucia la caja vieja y la nueva
static void DiffKeys(const Key *a, int na, const Key *b, int nb) {
    int n = na > nb ? na : nb;
    for (int i = 0; i < n; i++) {
        if (i < na && i < nb && SameKey(a[i], b[i])) continue;
        if (i < na) MarkRect(a[i].x0, a[i].y0, a[i].x1, a[i].y1);
        if (i < nb) MarkRect(b[i].x0, b[i].y0, b[i].x1, b[i].y1);
    }
}

// ---- copia a la pantalla
// Una columna (filas [y0,y1)) de la imagen en el formato de la pantalla, en orden de dirección creciente.
// src apunta a la columna en la fila 0 de la pantalla (la imagen tiene ancho s_W).
// v16.2: una versión por formato (antes se elegía el formato en CADA píxel) y tablas para pasar 5/6 bits a 8.
static u8 s_x5[32], s_x6[64];
static bool s_xInit = false;
template <int FMT> static int PackColumnT(const u16 *src, int y0, int y1, bool down, u8 *out) {
    int n = 0;
    const int rows = y1 - y0;
    const u16 *p = down ? src + (y1 - 1) * s_W : src + y0 * s_W;
    const int step = down ? -s_W : s_W;
    for (int k = 0; k < rows; k++, p += step) {
        u32 c = *p;
        if (FMT == R3D::FB_BGR8) {
            out[n] = s_x5[c & 31]; out[n + 1] = s_x6[(c >> 5) & 63]; out[n + 2] = s_x5[c >> 11]; n += 3;
        } else if (FMT == R3D::FB_RGBA8) {
            out[n] = 0xFF; out[n + 1] = s_x5[c & 31]; out[n + 2] = s_x6[(c >> 5) & 63]; out[n + 3] = s_x5[c >> 11]; n += 4;
        } else if (FMT == R3D::FB_RGB565) {
            out[n] = (u8)c; out[n + 1] = (u8)(c >> 8); n += 2;
        } else if (FMT == R3D::FB_RGB5A1) {
            u32 v = ((c >> 11) << 11) | ((((c >> 5) & 63) >> 1) << 6) | ((c & 31) << 1) | 1;
            out[n] = (u8)v; out[n + 1] = (u8)(v >> 8); n += 2;
        } else {
            u32 v = (((c >> 11) >> 1) << 12) | ((((c >> 5) & 63) >> 2) << 8) | (((c & 31) >> 1) << 4) | 0xF;
            out[n] = (u8)v; out[n + 1] = (u8)(v >> 8); n += 2;
        }
    }
    return n;
}
static inline int PackColumn(const R3D::Target &t, const u16 *src, int y0, int y1, u8 *out) {
    bool down = t.dy < 0;                                          // en la 3DS la fila siguiente suele estar ANTES
    switch (t.fmt) {
        case R3D::FB_BGR8:   return PackColumnT<R3D::FB_BGR8>(src, y0, y1, down, out);
        case R3D::FB_RGBA8:  return PackColumnT<R3D::FB_RGBA8>(src, y0, y1, down, out);
        case R3D::FB_RGB565: return PackColumnT<R3D::FB_RGB565>(src, y0, y1, down, out);
        case R3D::FB_RGB5A1: return PackColumnT<R3D::FB_RGB5A1>(src, y0, y1, down, out);
        default:             return PackColumnT<R3D::FB_RGBA4>(src, y0, y1, down, out);
    }
}
// Copia columnas [x0,x1) y filas [y0,y1) de 'img' (ancho s_W; img[y * s_W + x] = píxel de pantalla (x, y))
static void BlitRect(const R3D::Target &t, const u16 *img, int x0, int x1, int y0, int y1) {
    static u32 col[240];                                           // 240 filas x 4 bytes como máximo
    if (!s_xInit) {
        for (int i = 0; i < 32; i++) s_x5[i] = (u8)((i << 3) | (i >> 2));
        for (int i = 0; i < 64; i++) s_x6[i] = (u8)((i << 2) | (i >> 4));
        s_xInit = true;
    }
    for (int x = x0; x < x1; x++) {
        u8 *lo = t.base + x * t.dx + (t.dy < 0 ? (y1 - 1) : y0) * t.dy;   // dirección más baja de este trozo de columna
        int n = PackColumn(t, img + x, y0, y1, (u8 *)col);
        if (!((uintptr_t)lo & 3) && !(n & 3)) {
            volatile u32 *dd = (volatile u32 *)lo;
            for (int i = 0; i < n / 4; i++) dd[i] = col[i];
        } else {
            volatile u8 *dd = lo;
            const u8 *src = (const u8 *)col;
            for (int i = 0; i < n; i++) dd[i] = src[i];
        }
    }
}
static bool FastBlitOk(const R3D::Target &t) {
    int bpp = t.fmt == R3D::FB_RGBA8 ? 4 : t.fmt == R3D::FB_BGR8 ? 3 : 2;
    return (t.dy == bpp || t.dy == -bpp) && t.fmt <= R3D::FB_RGBA4;
}

static void DrawTexts(const Screen &s, const bool *redraw) {
    for (int i = 0; i < s_nt; i++) {
        if (redraw && !redraw[i]) continue;
        const TextItem &it = s_text[i];
        Color c(it.c.r, it.c.g, it.c.b, 255);
        if (it.small) {
            if (it.x < 0 || it.y < 0 || it.y > 230 || it.x > s_W - 6) continue;
            char a[64];
            snprintf(a, sizeof(a), "%s", it.s);
            size_t maxc = (size_t)((s_W - it.x) / 6);
            if (strlen(a) > maxc) a[maxc] = 0;
            s.Draw(a, (u32)it.x, (u32)it.y, c, Color(0, 0, 0, 0));
        } else {
            if (it.x >= s_W || it.y >= 226) continue;
            if (it.game) GameFont::Draw(s, it.s, it.x, it.y < 0 ? 0 : it.y, c, s_W);
            else s.DrawSysfont(it.s, (u32)(it.x < 0 ? 0 : it.x), (u32)(it.y < 0 ? 0 : it.y), c);
        }
    }
}

// Pantalla con un formato raro (columnas no contiguas): todo por franjas, píxel a píxel
static void EndSimple(const Screen &s, const R3D::Target &t, void (*after)(const Screen &)) {
    s_chUse = false;
    for (int y0 = 0; y0 < 240; y0 += STRIP_H) {
        int y1 = y0 + STRIP_H < 240 ? y0 + STRIP_H : 240;
        Dst d = { s_strip, s_W, 0, y0, y0, y1, 0, s_W };
        ComposeRegion(d);
        R3D::Blit565(t, s_strip, s_W, y1 - y0, 0, y0);
    }
    if (after) after(s);
    DrawTexts(s, nullptr);
}

void End(const Screen &s, void (*after)(const Screen &), u32 extra) {
    R3D::Target t;
    if (!Overlay::MakeTarget(s, t) || t.w != s_W) { s_afterW = 0; return; }
    s_frameOf[s_top ? 0 : 1]++;
    // v16.2: aparición del menú (ver "Aparición del menú") y transición de página (ver "PÁGINA GUARDADA")
    bool useSnap = false, useFull = false;
    const bool opening = s_open < 0.999f || s_ody;
    if (opening) {
        u64 ts = svcGetSystemTick();
        s_fullState = BuildFull();
        useFull = s_fullState != 0;
        TransformAll();
        g_prof.compose += svcGetSystemTick() - ts;
        if (useFull) g_prof.pageFrames++;
    } else {
        Snap &S = s_snap[s_top ? 0 : 1];                          // tras la aparición: fuera la pantalla completa
        if (S.cap > 70000) { free(S.buf); S.buf = nullptr; S.cap = 0; S.valid = false; S.key = 0; }   // (en píxeles: la
        // pantalla entera son 96000/76800; la página guardada nunca pasa de ~51000)
        S.restarts = 0;
    }
    if (!opening && (s_pg < 0.999f || s_pdx || s_pdy)) {
        u64 ts = svcGetSystemTick();
        useSnap = BuildSnap();
        TransformPage();
        g_prof.compose += svcGetSystemTick() - ts;
        if (useSnap) g_prof.pageFrames++;
    }
    // ¿Ya está esto mismo en el búfer donde se va a pintar? Entonces no hay nada que hacer.
    u32 sig = ListSig(extra);
    BufState *b = nullptr;
    for (auto &e : s_buf) if (e.base == t.base) b = &e;
    if (b && b->sig == sig && !b->full) { b->use = ++s_bufUse; g_prof.skips++; s_afterW = 0; return; }
    if (!b) {
        b = &s_buf[0];
        for (auto &e : s_buf) if (e.use < b->use) b = &e;
        b->base = t.base; b->full = true;
    }
    g_prof.draws++;
    u64 tc0 = svcGetSystemTick();
    if (!FastBlitOk(t)) {
        EndSimple(s, t, after);
        g_prof.compose += svcGetSystemTick() - tc0;
        b->sig = sig; b->use = ++s_bufUse; b->full = true;        // sin huellas: la próxima vez, otra vez entero
        s_afterW = 0;
        return;
    }
    BuildKeys();
    if (opening) s_chUse = false;                                  // los paneles cambian en cada cuadro: sin caché
    else PrepareChrome();

    const int tw = TilesW();
    // 1) qué difiere de lo que muestra ESTE búfer
    memset(s_dirty, 0, sizeof(s_dirty));
    bool redraw[MAX_TEXTS];
    memset(redraw, 0, sizeof(redraw));
    bool hasAfter = after && s_afterW > 0;
    if (b->full) { MarkRect(0, 0, s_W, 240); for (int i = 0; i < s_nt; i++) redraw[i] = true; }
    else {
        DiffKeys(b->shapes, b->ns, s_curS, s_ns);
        DiffKeys(b->texts, b->nt, s_curT, s_nt);
        if (hasAfter && extra != b->extra) MarkRect(s_afterX, s_afterY, s_afterX + s_afterW, s_afterY + s_afterH);
        // lo que pintó el "after" (el modelo 3D) en ESTE búfer: si ya no está, se movió o cambió, hay que borrarlo
        bool sameAfter = hasAfter && b->aw == s_afterW && b->ah == s_afterH && b->ax == s_afterX && b->ay == s_afterY && extra == b->extra;
        if (b->aw > 0 && !sameAfter) MarkRect(b->ax, b->ay, b->ax + b->aw, b->ay + b->ah);
        // un texto que toca una zona que se repinta se borra y se vuelve a escribir ENTERO (el viejo y el nuevo)
        for (int pass = 0; pass < 8; pass++) {
            bool grew = false;
            for (int i = 0; i < b->nt; i++) {
                const Key &k = b->texts[i];
                if (AnyRect(k.x0, k.y0, k.x1, k.y1)) grew |= GrowRect(k.x0, k.y0, k.x1, k.y1);
            }
            for (int i = 0; i < s_nt; i++) {
                const Key &k = s_curT[i];
                if (AnyRect(k.x0, k.y0, k.x1, k.y1)) { redraw[i] = true; grew |= GrowRect(k.x0, k.y0, k.x1, k.y1); }
            }
            if (hasAfter && AnyRect(s_afterX, s_afterY, s_afterX + s_afterW, s_afterY + s_afterH))
                grew |= GrowRect(s_afterX, s_afterY, s_afterX + s_afterW, s_afterY + s_afterH);
            if (!grew) break;
        }
    }
    u64 tc1 = svcGetSystemTick();
    // 2) recomponer y copiar SOLO los cuadros sucios: fila de cuadros a fila, en tramos horizontales
    u64 tblit = 0;
    for (int ty = 0; ty < TH; ty++) {
        for (int tx = 0; tx < tw;) {
            if (!s_dirty[ty][tx]) { tx++; continue; }
            int run = tx;
            while (run < tw && s_dirty[ty][run]) run++;
            int y0 = ty * TILE, y1 = y0 + TILE;
            Dst d = { s_strip, s_W, 0, y0, y0, y1, tx * TILE, run * TILE };
            if (useFull) ComposeFull(d);
            else if (useSnap) ComposeSnap(d);
            else ComposeRegion(d);
            u64 tb = svcGetSystemTick();
            BlitRect(t, s_strip - y0 * s_W, tx * TILE, run * TILE, y0, y1);
            tblit += svcGetSystemTick() - tb;
            tx = run;
        }
    }
    u64 tc2 = svcGetSystemTick();
    g_prof.compose += (tc2 - tc1) - tblit + (tc1 - tc0);
    g_prof.blit += tblit;
    if (after && (b->full || (hasAfter && AnyRect(s_afterX, s_afterY, s_afterX + s_afterW, s_afterY + s_afterH)))) after(s);
    u64 tc3 = svcGetSystemTick();
    g_prof.after += tc3 - tc2;
    DrawTexts(s, redraw);
    g_prof.text += svcGetSystemTick() - tc3;
    // 3) este búfer ya muestra exactamente esta lista
    memcpy(b->shapes, s_curS, (size_t)s_ns * sizeof(Key));
    memcpy(b->texts, s_curT, (size_t)s_nt * sizeof(Key));
    b->ns = s_ns; b->nt = s_nt;
    b->extra = extra;
    if (hasAfter) { b->ax = s_afterX; b->ay = s_afterY; b->aw = s_afterW; b->ah = s_afterH; }
    else b->aw = b->ah = 0;
    b->full = false;
    b->sig = sig;
    b->use = ++s_bufUse;
    s_afterW = 0;
}

// ---------------------------------------------------------------------------------------------
// Fondo: una muestra por bloque de 4x4, dos pasadas de desenfoque de caja (≈ gaussiano), y luego un velo azul
// marino (Apple: "atenuar para centrar la atención" en una tarea modal).
// v16.2: antes eran 4 muestras por bloque leídas byte a byte (12 lecturas del framebuffer por bloque, y en Azahar
// cada una pasa por el camino lento: 67 ms al abrir el menú). Ahora 1 lectura alineada de 32 bits por bloque, en la
// fila del bloque cuya dirección cae alineada; el desenfoque posterior hace invisible la diferencia.
static bool CaptureOne(const Screen &s, int idx) {
    R3D::Target t;
    s_bgOk[idx] = false;
    if (!Overlay::MakeTarget(s, t)) return false;
    const int W = t.w, LW = W / 4;
    u8 *bg = s_bg[idx];
    u16 *tmp = s_strip;                                            // aún no se dibuja nada: sirve de memoria temporal
    long sum = 0, sum2 = 0;
    for (int ly = 0; ly < LH; ly++)
        for (int lx = 0; lx < LW; lx++) {
            int sx = lx * 4 + 2, sy = ly * 4 + 1;
            for (int o = 0; o < 4; o++) {                         // la fila del bloque con dirección alineada
                uintptr_t ad = (uintptr_t)(t.base + sx * t.dx + (ly * 4 + o) * t.dy);
                if (!(ad & 3)) { sy = ly * 4 + o; break; }
            }
            u8 r, g, b;
            R3D::GetPixelFast(t, sx, sy, r, g, b);
            u8 *p = bg + (ly * LW + lx) * 3;
            p[0] = r; p[1] = g; p[2] = b;
            int l = (p[0] * 3 + p[1] * 6 + p[2]) / 10;
            sum += l; sum2 += l * l;
        }
    long n = (long)LW * LH;
    long mean = sum / n, var = sum2 / n - mean * mean;
    if (mean < 4 && var < 16) return false;                       // pantalla negra: mejor el degradado
    for (int pass = 0; pass < 2; pass++) {
        for (int ly = 0; ly < LH; ly++)                            // horizontal (radio 2)
            for (int lx = 0; lx < LW; lx++)
                for (int k = 0; k < 3; k++) {
                    int a = 0;
                    for (int dd = -2; dd <= 2; dd++) { int xx = lx + dd; xx = xx < 0 ? 0 : xx >= LW ? LW - 1 : xx; a += bg[(ly * LW + xx) * 3 + k]; }
                    tmp[(ly * LW + lx) * 3 + k] = (u16)(a / 5);
                }
        for (int ly = 0; ly < LH; ly++)                            // vertical (radio 2)
            for (int lx = 0; lx < LW; lx++)
                for (int k = 0; k < 3; k++) {
                    int a = 0;
                    for (int dd = -2; dd <= 2; dd++) { int yy = ly + dd; yy = yy < 0 ? 0 : yy >= LH ? LH - 1 : yy; a += tmp[(yy * LW + lx) * 3 + k]; }
                    bg[(ly * LW + lx) * 3 + k] = (u8)(a / 5);
                }
    }
    for (int ly = 0; ly < LH; ly++) {
        float t2 = ly / (float)(LH - 1);
        float nr = 10 + 12 * t2, ng = 16 + 18 * t2, nb = 36 + 34 * t2;   // azul marino del menú
        for (int lx = 0; lx < LW; lx++) {
            u8 *p = bg + (ly * LW + lx) * 3;
            float l = (p[0] * 3 + p[1] * 6 + p[2]) / 10.f;
            float r = l + (p[0] - l) * 0.75f, g = l + (p[1] - l) * 0.75f, b = l + (p[2] - l) * 0.75f;   // algo menos de color
            p[0] = (u8)(r * 0.38f + nr * 0.62f);
            p[1] = (u8)(g * 0.38f + ng * 0.62f);
            p[2] = (u8)(b * 0.38f + nb * 0.62f + 4.f);
        }
    }
    s_bgOk[idx] = true;
    return true;
}

// Precalcula el fondo a tamaño real (una vez): así cada cuadro solo lo copia
static void Prebake(int idx) {
    const int W = idx == 0 ? 400 : 320;
    if (!s_bgFull[idx]) s_bgFull[idx] = (u16 *)malloc((size_t)W * 240 * sizeof(u16));
    if (!s_bgFull[idx]) return;                                    // sin memoria: se calcula al componer
    bool top = s_top; int w = s_W;
    s_top = idx == 0; s_W = W;
    Dst d = { s_bgFull[idx], W, 0, 0, 0, 240, 0, W };
    s_bgFull[idx] = nullptr;                                       // ComputeBg, no copiarse a sí mismo
    ComputeBg(d);
    s_bgFull[idx] = d.buf;
    s_top = top; s_W = w;
}

void CaptureBackground(const Screen &top, const Screen &bottom) {
    Invalidate();
    CaptureOne(top, 0);
    CaptureOne(bottom, 1);
    s_bgTried[0] = s_bgTried[1] = false;
    s_chLogged = false;
    for (auto &S : s_snap) S.restarts = 0;
    for (auto &C : s_ch) { C.tried = false; C.why = ""; }
    Log::Write("menu: fondo %s/%s", s_bgOk[0] ? "juego" : "degradado", s_bgOk[1] ? "juego" : "degradado");
}

void ReleaseBackground() {
    for (auto &p : s_bgFull) { free(p); p = nullptr; }
    for (auto &C : s_ch) { free(C.buf); C.buf = nullptr; C.cap = 0; C.ok = false; }
    for (auto &S : s_snap) { free(S.buf); S.buf = nullptr; S.cap = 0; S.valid = false; }
}

} // namespace UiGfx
