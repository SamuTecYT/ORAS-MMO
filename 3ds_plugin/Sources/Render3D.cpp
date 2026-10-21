/*
 * Render3D.cpp — ORAS MMO v13
 * Avatares 3D por software con el presupuesto de CPU de la 3DS en mente:
 *  - cada avatar se rasteriza en su sprite (RGB565, 0 = transparente) con z-buffer local, textura
 *    del juego, sombreado toon y contorno; en cada cuadro el sprite solo se compone en pantalla;
 *  - se re-rasteriza solo si cambió la pose o la vista (un avatar quieto casi no cuesta);
 *  - un presupuesto de ciclos por cuadro, que se ajusta midiendo los FPS reales del juego, reparte
 *    los re-rasterizados entre los avatares que se mueven.
 *
 * Tubería entera (cada instrucción del ARM11 cuesta lo mismo, así que se cuentan instrucciones):
 *  - una vez por modelo se precalcula una caché: vértices únicos (posición + normal + piel; las
 *    costuras de UV comparten transformación), agrupados por combinación de huesos/pesos, y los
 *    triángulos ya en orden de dibujo con sus UV en punto fijo (las UV no cambian nunca);
 *  - por cuadro se mezcla una matriz por combinación (proyección y luz ya dentro) y cada vértice
 *    único se transforma UNA vez a un formato compacto: x,y de pantalla 28.4 empaquetadas en 32 bits
 *    (con sesgo para que sean positivas) y z 8.8 + sombreado empaquetados en otros 32 bits;
 *  - triángulos y píxeles son solo enteros: SIMD de 16 bits del ARMv6 (ssub16, smusdx, smlad) para
 *    área, caja, funciones de borde e interpolación baricéntrica (un recíproco por triángulo).
 * Los triángulos del modelo ocupan ~1 píxel en pantalla: aritmética entera de bordes (28.4) con
 * regla de relleno, atributos afines (la variación de w dentro de un avatar es < 5 %).
 */
#pragma GCC optimize("O2")
#include "Render3D.hpp"
#include "SkinColors.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32) && defined(__ARM_FEATURE_SAT)
#include <arm_acle.h>
#define R3D_SIMD 1
#else
#define R3D_SIMD 0
#endif
#if defined(__arm__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
#define R3D_VCVT 1
#else
#define R3D_VCVT 0
#endif

namespace R3D {

static const int   SW = 400, SH = 240;
static const int   MARGIN = 48;                 // el sprite puede salirse de la pantalla hasta este margen
static const int   MAX_SPR_PIX = 200 * 240;     // más grande = avatar pegado a la cámara: no se dibuja
static const float MIN_BUDGET = 500000.f, MAX_BUDGET = 1400000.f;
// Coordenadas de pantalla en 28.4 con sesgo: X = 16 * (x + XB), Y = 16 * (y + YB); el centro de la pantalla
// queda en 16384. Un vértice es válido si X e Y están en [VMIN, VMAX): x en [-568, 968), y en [-648, 888).
// Así todas las diferencias caben en int16 y un vértice inválido (guardado como 0,0) siempre forma un
// triángulo de más de SPAN_MAX de lado, que se descarta sin mirar ninguna bandera.
static const int   XB = 824, YB = 904;
static const int   VMIN = 4096, VMAX = 28672;
static const int   SPAN_MAX = 4096;             // 256 px: lado máximo de un triángulo en 28.4 (sin desbordes)

Stats g_stats;

struct Sprite {
    uint16_t *px;
    int       cap;
    bool      valid;
    int       x0, y0, w, h;                    // rectángulo en pantalla al rasterizar
    // firma de lo rasterizado
    const Hero::Model *model;
    const Hero::Model *attach;
    int       anim, prevAnim;
    float     frame, prevFrame, blend;
    bool      blink;
    uint8_t   variant, acc;
    float     fx, fy;                          // pies en pantalla
    float     hx, hy, rx, ry, gx, gy;          // pies->cabeza, pies->derecha, pies->frente (pantalla)
    float     headX, headY;
    uint32_t  lastRender;
    uint32_t  cost;                            // ticks del último rasterizado
};

struct ViewSig { bool ok; float fx, fy, fw, hx, hy, rx, ry, gx, gy; };
// Por hueso (o combinación de huesos): filas X', Y', W y luz. X'/W e Y'/W dan directamente X e Y de
// pantalla en 28.4 con sesgo; la fila de luz da 64 * (n·L) + 64, el índice de la tabla de sombreado.
struct BoneXf { float m[16]; };

// ---------------------------------------------------------------------------------------------
// Caché por modelo (se construye la primera vez que se dibuja; solo depende de los datos del modelo)
struct Combo { uint8_t n, bone[4], raw[4]; float w[4]; };
// Triángulo en orden de dibujo (A, B, C) = índices originales (0, 2, 1): de cara si el área es > 0.
// ab = A | B << 16, c = C (vértices únicos). UV en punto fijo: u0/v0 de A en 2^-30 (v invertida: -v, que
// equivale a 1 - v al repetir la textura), du/dv = (B - A, C - A) en int16 de 2^-16.
struct TriRec { uint32_t a, b, c; int32_t u0, v0; uint32_t du, dv; };
struct RangeRec { uint32_t first, count; uint8_t tex, face, head; };
struct ModelCache {
    const Hero::Model    *model;
    const Hero::Vertex   *verts;
    const uint16_t       *indices;
    int       vertCount, indexCount, rangeCount, boneCount;
    int       nU, nC, nTris, nRanges, trisTotal;
    float    *upos;                            // 6 floats por vértice único (posición, normal), por combinación
    uint16_t *ccount;                          // vértices únicos de cada combinación
    Combo    *combos;
    TriRec   *tris;
    RangeRec  ranges[Hero::MAX_RANGES];
    uint32_t  bytes;                           // memoria que ocupa (sin los temporales de BuildCache)
};
static const int MAX_CACHE = 4;
static ModelCache s_mc[MAX_CACHE];

static Sprite    s_spr[MAX_SLOTS + 1];
static uint16_t *s_z = nullptr;                // z local del sprite en curso (crece según haga falta)
static int       s_zCap = 0;                   // v16.3: antes 94 KB fijos de golpe; sin un hueco así de grande
                                               // (memoria fragmentada) no se dibujaba NADA: ni amigos ni vista previa
static bool      s_inited = false;
static uint32_t *s_vxy = nullptr, *s_vzs = nullptr;   // salida de los vértices únicos
static int       s_vCap = 0;
static BoneXf   *s_cbuf = nullptr;             // matrices de las combinaciones de varios huesos
static const float **s_cm = nullptr;           // matriz de cada combinación
static int       s_cCap = 0;
static uint32_t  s_shadeLut[256];              // índice 64 * (n·L + 1) (saturado a 0..255) -> 256 * luz
static uint32_t  s_frameNo = 0;

static uint64_t (*s_clock)() = nullptr;
static uint32_t  s_tps = 268111856;
static float     s_budget = 1000000.f, s_ceiling = 1200000.f;
static float     s_cap = MAX_BUDGET;                  // v16: tope según la calidad (Perf::Level)
static uint64_t  s_spentLast = 0;
static int       s_goodFrames = 0, s_fastFrames = 0;

// Luz principal (desde arriba y algo hacia la cámara), como el juego
static const Vec3 kLight = { 0.28f, 0.86f, 0.42f };

// ---------------------------------------------------------------------------------------------
// Enteros empaquetados (dos int16 en 32 bits). En ARM son instrucciones SIMD del ARMv6; en PC, C portable
// con la misma semántica (sin desbordes con signo).
#if R3D_SIMD
static inline uint32_t Sub16(uint32_t a, uint32_t b) { return (uint32_t)__ssub16((int16x2_t)a, (int16x2_t)b); }
// a.x * b.y - a.y * b.x  (+ acc)
static inline int32_t Cross16(uint32_t a, uint32_t b) { return __smusdx((int16x2_t)a, (int16x2_t)b); }
static inline int32_t CrossAcc16(uint32_t a, uint32_t b, int32_t acc) { return __smlsdx((int16x2_t)a, (int16x2_t)b, acc); }
// a.x * b.x - a.y * b.y
static inline int32_t DotSub16(uint32_t a, uint32_t b) { return __smusd((int16x2_t)a, (int16x2_t)b); }
// acc + a.x * b.x + a.y * b.y
static inline int32_t Dot16(uint32_t a, uint32_t b, int32_t acc) { return __smlad((int16x2_t)a, (int16x2_t)b, acc); }
static inline void MinMax16(uint32_t &mn, uint32_t &mx, uint32_t v) {
    __ssub16((int16x2_t)v, (int16x2_t)mn); mn = __sel(mn, v);
    __ssub16((int16x2_t)v, (int16x2_t)mx); mx = __sel(v, mx);
}
static inline uint32_t Usat16(int32_t v) { return __usat(v, 16); }
static inline uint32_t Usat8(int32_t v) { return __usat(v, 8); }
#else
static inline int32_t Lo16(uint32_t a) { return (int16_t)(uint16_t)(a & 0xFFFF); }
static inline int32_t Hi16(uint32_t a) { return (int16_t)(uint16_t)(a >> 16); }
static inline int32_t Wrap32(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }
static inline uint32_t Sub16(uint32_t a, uint32_t b) { return ((a - b) & 0xFFFFu) | (((a >> 16) - (b >> 16)) << 16); }
static inline int32_t Cross16(uint32_t a, uint32_t b) { return Wrap32((int64_t)Lo16(a) * Hi16(b) - (int64_t)Hi16(a) * Lo16(b)); }
static inline int32_t CrossAcc16(uint32_t a, uint32_t b, int32_t acc) { return Wrap32((int64_t)acc + (int64_t)Lo16(a) * Hi16(b) - (int64_t)Hi16(a) * Lo16(b)); }
static inline int32_t DotSub16(uint32_t a, uint32_t b) { return Wrap32((int64_t)Lo16(a) * Lo16(b) - (int64_t)Hi16(a) * Hi16(b)); }
static inline int32_t Dot16(uint32_t a, uint32_t b, int32_t acc) { return Wrap32((int64_t)acc + (int64_t)Lo16(a) * Lo16(b) + (int64_t)Hi16(a) * Hi16(b)); }
static inline void MinMax16(uint32_t &mn, uint32_t &mx, uint32_t v) {
    uint32_t lo = Lo16(v) < Lo16(mn) ? (v & 0xFFFF) : (mn & 0xFFFF), hi = Hi16(v) < Hi16(mn) ? (v >> 16) : (mn >> 16);
    mn = lo | (hi << 16);
    lo = Lo16(v) >= Lo16(mx) ? (v & 0xFFFF) : (mx & 0xFFFF); hi = Hi16(v) >= Hi16(mx) ? (v >> 16) : (mx >> 16);
    mx = lo | (hi << 16);
}
static inline uint32_t Usat16(int32_t v) { return v < 0 ? 0 : v > 65535 ? 65535 : (uint32_t)v; }
static inline uint32_t Usat8(int32_t v) { return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v; }
#endif

// float -> int truncando, con saturación (NaN -> 0). En ARM es la propia instrucción vcvt, que satura por
// hardware (sin conducta indefinida de C); en PC se protege a mano.
static inline int32_t CvtSat(float f) {
#if R3D_VCVT
    float t;
    int32_t r;
    __asm__("vcvt.s32.f32 %0, %1" : "=t"(t) : "t"(f));
    __asm__("vmov %0, %1" : "=r"(r) : "t"(t));
    return r;
#else
    if (!(f == f)) return 0;
    if (f >= 2147483520.f) return 2147483647;
    if (f <= -2147483648.f) return (-2147483647 - 1);
    return (int32_t)f;
#endif
}

// (hi & 0xFFFF0000) | (lo >> 16): una instrucción pkhtb en ARM
static inline uint32_t PackHiHi(uint32_t hi, uint32_t lo) {
#if R3D_SIMD
    uint32_t r;
    __asm__("pkhtb %0, %1, %2, asr #16" : "=r"(r) : "r"(hi), "r"(lo));
    return r;
#else
    return (hi & 0xFFFF0000u) | (lo >> 16);
#endif
}
// Oculta un valor al optimizador (evita que convierta productos en variables de inducción que no caben
// en los registros); no genera ninguna instrucción.
static inline uint32_t Opaque(uint32_t v) {
#if defined(__GNUC__)
    __asm__("" : "+r"(v));
#endif
    return v;
}

static inline float Smooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

static void FreeCache(ModelCache &C) {
    free(C.upos); free(C.ccount); free(C.combos); free(C.tris);
    memset(&C, 0, sizeof(C));
}

bool Init() {
    s_inited = true;
    for (int i = 0; i < 256; i++) {
        float nl = (i > 128 ? 128 : i) / 64.f - 1.f;
        s_shadeLut[i] = (uint32_t)(int)(256.f * (0.70f + 0.30f * Smooth(-0.05f, 0.18f, nl)) + 0.5f);
    }
    memset(s_spr, 0, sizeof(s_spr));
    return true;
}

void Shutdown() {
    for (int i = 0; i <= MAX_SLOTS; i++) { free(s_spr[i].px); s_spr[i].px = nullptr; s_spr[i].cap = 0; s_spr[i].valid = false; }
    free(s_z); s_z = nullptr; s_zCap = 0;
    s_inited = false;
    free(s_vxy); free(s_vzs); s_vxy = s_vzs = nullptr; s_vCap = 0;
    free(s_cbuf); free(s_cm); s_cbuf = nullptr; s_cm = nullptr; s_cCap = 0;
    for (int i = 0; i < MAX_CACHE; i++) FreeCache(s_mc[i]);
}

// v16.3: suelta lo que se rehace solo y barato (sprites, z, vértices del cuadro) pero CONSERVA las cachés de los
// modelos: rehacerlas necesita ~190 KB temporales en bloques de hasta ~85 KB y, con la memoria troceada por el menú,
// fallaba sin avisar: no se dibujaba la vista previa del menú ni los amigos de color o en bici (pruebas 2026-10-18).
// Se construyen UNA vez al arrancar (Prewarm), con la memoria entera.
void ReleaseMemory() {
    for (int i = 0; i <= MAX_SLOTS; i++) { free(s_spr[i].px); s_spr[i].px = nullptr; s_spr[i].cap = 0; s_spr[i].valid = false; }
    free(s_z); s_z = nullptr; s_zCap = 0;
    free(s_vxy); free(s_vzs); s_vxy = s_vzs = nullptr; s_vCap = 0;
    free(s_cbuf); free(s_cm); s_cbuf = nullptr; s_cm = nullptr; s_cCap = 0;
}


void ForgetAll() {
    for (int i = 0; i <= MAX_SLOTS; i++) s_spr[i].valid = false;
}

void SetClock(uint64_t (*now)(), uint32_t tps) { s_clock = now; if (tps) s_tps = tps; }

void SetBudgetCap(float cap) { s_cap = cap < MIN_BUDGET ? MIN_BUDGET : cap > MAX_BUDGET ? MAX_BUDGET : cap; }

void FrameFeedback(uint64_t period) {
    if (!s_clock || !period) return;
    float vblank = (float)s_tps / 59.83f;
    int vb = (int)((float)period / vblank + 0.5f);
    if (vb > 8) return;                                // pausa, carga o menú del plugin: no cuenta
    g_stats.frames++;
    if (vb <= 1) s_fastFrames = 90;                    // el juego va a 60 fps aquí
    else if (s_fastFrames > 0) s_fastFrames--;
    int base = s_fastFrames > 0 ? 1 : 2;
    // Solo nos culpamos si de verdad gastamos mucho ese cuadro: el juego tiene tirones propios y antes
    // cada uno recortaba el presupuesto hasta dejarlo en 1 avatar cada 2 cuadros.
    bool busy = s_spentLast > 400000 && (float)s_spentLast > s_budget * 0.5f;
    if (vb > base && busy) {                           // se perdió un cuadro mientras rasterizábamos mucho
        g_stats.slowFrames++;
        s_ceiling = s_budget * 0.95f;
        s_budget *= 0.85f;
        if (s_budget < MIN_BUDGET) s_budget = MIN_BUDGET;
        if (s_ceiling < MIN_BUDGET) s_ceiling = MIN_BUDGET;
        s_goodFrames = 0;
    } else if (vb <= base) {
        s_goodFrames++;
        if (s_goodFrames > 10) {
            s_budget *= 1.03f;
            if (s_budget > s_ceiling) s_budget = s_ceiling;
        }
        if (s_goodFrames % 30 == 0) {                  // probar de nuevo poco a poco
            s_ceiling *= 1.02f;
            if (s_ceiling > MAX_BUDGET) s_ceiling = MAX_BUDGET;
        }
    }
    if (s_ceiling > s_cap) s_ceiling = s_cap;
    if (s_budget > s_cap) s_budget = s_cap;
    g_stats.budgetK = (int)(s_budget / 1000.f);
}

// ---------------------------------------------------------------------------------------------
// Píxeles
static inline uint8_t *PixAddr(const Target &t, int x, int y) { return t.base + x * t.dx + y * t.dy; }

void PutPixel(const Target &t, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *p = PixAddr(t, x, y);
    switch (t.fmt) {
        case FB_RGBA8: p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r; break;
        case FB_BGR8:  p[0] = b; p[1] = g; p[2] = r; break;
        case FB_RGB565: { uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); p[0] = v & 0xFF; p[1] = v >> 8; break; }
        case FB_RGB5A1: { uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1); p[0] = v & 0xFF; p[1] = v >> 8; break; }
        case FB_RGBA4:  { uint16_t v = (uint16_t)(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | 0xF); p[0] = v & 0xFF; p[1] = v >> 8; break; }
    }
}

void GetPixelFast(const Target &t, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) {
    const uint8_t *p = PixAddr(t, x, y);
    uintptr_t a = (uintptr_t)p;
    if (t.fmt == FB_BGR8 && !(a & 3)) {            // B,G,R + el byte siguiente en una sola lectura
        uint32_t w = *(const volatile uint32_t *)p;
        b = (uint8_t)w; g = (uint8_t)(w >> 8); r = (uint8_t)(w >> 16);
        return;
    }
    if ((t.fmt == FB_RGB565 || t.fmt == FB_RGB5A1 || t.fmt == FB_RGBA4) && !(a & 1)) {
        uint16_t v = *(const volatile uint16_t *)p;
        if (t.fmt == FB_RGB565) { r = (uint8_t)((v >> 11) << 3); g = (uint8_t)(((v >> 5) & 63) << 2); b = (uint8_t)((v & 31) << 3); }
        else if (t.fmt == FB_RGB5A1) { r = (uint8_t)((v >> 11) << 3); g = (uint8_t)(((v >> 6) & 31) << 3); b = (uint8_t)(((v >> 1) & 31) << 3); }
        else { r = (uint8_t)((v >> 12) * 17); g = (uint8_t)(((v >> 8) & 15) * 17); b = (uint8_t)(((v >> 4) & 15) * 17); }
        return;
    }
    GetPixel(t, x, y, r, g, b);
}

void GetPixel(const Target &t, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) {
    const uint8_t *p = PixAddr(t, x, y);
    uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
    switch (t.fmt) {
        case FB_RGBA8: b = p[1]; g = p[2]; r = p[3]; break;
        case FB_BGR8:  b = p[0]; g = p[1]; r = p[2]; break;
        case FB_RGB565: r = (uint8_t)((v >> 11) << 3); g = (uint8_t)(((v >> 5) & 63) << 2); b = (uint8_t)((v & 31) << 3); break;
        case FB_RGB5A1: r = (uint8_t)((v >> 11) << 3); g = (uint8_t)(((v >> 6) & 31) << 3); b = (uint8_t)(((v >> 1) & 31) << 3); break;
        default:        r = (uint8_t)((v >> 12) * 17); g = (uint8_t)(((v >> 8) & 15) * 17); b = (uint8_t)(((v >> 4) & 15) * 17); break;
    }
}

// RGB565 -> formato del framebuffer
template <int FMT> static inline void Store(uint8_t *p, uint32_t c) {
    uint32_t r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
    if (FMT == FB_BGR8 || FMT == FB_RGBA8) {
        uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2)), g = (uint8_t)((g6 << 2) | (g6 >> 4)), b = (uint8_t)((b5 << 3) | (b5 >> 2));
        if (FMT == FB_BGR8) { p[0] = b; p[1] = g; p[2] = r; }
        else { p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r; }
    } else if (FMT == FB_RGB565) {
        p[0] = (uint8_t)c; p[1] = (uint8_t)(c >> 8);
    } else if (FMT == FB_RGB5A1) {
        uint32_t v = (r5 << 11) | ((g6 >> 1) << 6) | (b5 << 1) | 1;
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    } else {
        uint32_t v = ((r5 >> 1) << 12) | ((g6 >> 2) << 8) | ((b5 >> 1) << 4) | 0xF;
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    }
}

// Copia el rectángulo [xa..xb]x[ya..yb] de la pantalla desde img (origen de img en X0,Y0).
template <int FMT, bool KEY> static void CopyT(const Target &t, const uint16_t *img, int iw, int X0, int Y0,
                                              int xa, int xb, int ya, int yb) {
    const int dx = t.dx, dy = t.dy;
    for (int x = xa; x <= xb; x++) {
        const uint16_t *src = img + (ya - Y0) * iw + (x - X0);
        uint8_t *p = t.base + x * dx + ya * dy;
        for (int y = ya; y <= yb; y++, src += iw, p += dy) {
            uint32_t c = *src;
            if (KEY && !c) continue;
            Store<FMT>(p, c);
        }
    }
}

// v17.0 r2: máscara de "la escena está delante" del rectángulo de un dibujo, por columnas (un bit por fila), ENSANCHADA
// 2 píxeles en cada dirección: entre la profundidad y la imagen del juego los bordes de tejados y paredes pueden no
// coincidir por 1-2 píxeles (Azahar reduce cada una a su manera si dibuja a más resolución, y lo que se mueve va un poco
// por delante), y por esa rendija se veían "partículas" del Pokémon que estaba detrás. Se arma UNA vez por dibujo
// (leer la profundidad del rectángulo) y después cada píxel es un bit: más barato que la prueba de antes.
static const int ZM_COLS = 404, ZM_WORDS = 8;            // hasta 400 + 4 columnas y 240 + 4 filas (8 x 32 bits)
static uint32_t s_zm[ZM_COLS][ZM_WORDS], s_zd[ZM_COLS][ZM_WORDS];
static int s_zmX0 = 0, s_zmY0 = 0;

// Para el dibujo en [xa,xb]x[ya,yb]: false si la escena no tiene nada delante (se pinta sin prueba, lo normal)
static bool BuildZMask(const Target &t, int xa, int xb, int ya, int yb) {
    const int R = 2;
    const int x0 = xa - R, x1 = xb + R, y0 = ya - R, y1 = yb + R;
    int rx0 = x0 > t.zx0 ? x0 : t.zx0, rx1 = x1 < t.zx1 - 1 ? x1 : t.zx1 - 1;
    int ry0 = y0 > t.zy0 ? y0 : t.zy0, ry1 = y1 < t.zy1 - 1 ? y1 : t.zy1 - 1;
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > 399) rx1 = 399;
    if (ry1 > 239) ry1 = 239;
    if (rx0 > rx1 || ry0 > ry1) return false;
    const int W = x1 - x0 + 1, nw = ((y1 - y0) >> 5) + 1;
    if (W > ZM_COLS || nw > ZM_WORDS) return false;
    const uint32_t thr = t.zthr;
    {   // vistazo rápido (1 de cada 4 puntos): en campo abierto no hay nada delante y no hace falta más
        bool seen = false;
        for (int y = ry0; y <= ry1 && !seen; y += 2) {
            const uint32_t *r = t.zbuf + y * 400;
            for (int x = rx0; x <= rx1; x += 2) if (r[x] < thr) { seen = true; break; }
        }
        if (!seen) return false;
    }
    for (int c = 0; c < W; c++) for (int k = 0; k < nw; k++) s_zm[c][k] = 0;
    bool any = false;
    for (int y = ry0; y <= ry1; y++) {                        // por filas: la profundidad está en filas de 400
        const uint32_t *r = t.zbuf + y * 400;
        const int b = y - y0, wi = b >> 5;
        const uint32_t bit = 1u << (b & 31);
        for (int x = rx0; x <= rx1; x++) if (r[x] < thr) { s_zm[x - x0][wi] |= bit; any = true; }
    }
    if (!any) return false;
    for (int c = 0; c < W; c++) {                             // a lo alto: bit i <- bits i-2..i+2
        uint32_t *m = s_zm[c], tmp[ZM_WORDS];
        for (int k = 0; k < nw; k++) {
            const uint32_t v = m[k], lo = k > 0 ? m[k - 1] : 0, hi = k + 1 < nw ? m[k + 1] : 0;
            tmp[k] = v | (v << 1) | (lo >> 31) | (v << 2) | (lo >> 30) | (v >> 1) | (hi << 31) | (v >> 2) | (hi << 30);
        }
        for (int k = 0; k < nw; k++) m[k] = tmp[k];
    }
    for (int c = 0; c < W; c++) {                             // a lo ancho: columnas c-2..c+2
        const int a = c > 1 ? c - 2 : 0, e = c + 2 < W ? c + 2 : W - 1;
        for (int k = 0; k < nw; k++) {
            uint32_t v = 0;
            for (int q = a; q <= e; q++) v |= s_zm[q][k];
            s_zd[c][k] = v;
        }
    }
    s_zmX0 = x0; s_zmY0 = y0;
    return true;
}
static inline const uint32_t *MaskCol(int x) { return s_zd[x - s_zmX0]; }
static inline bool MaskBit(const uint32_t *mc, int y) { const int b = y - s_zmY0; return (mc[b >> 5] >> (b & 31)) & 1; }

// Lo que dio la prueba en el último dibujo (para "casi entero detrás": R3D::MostlyHidden)
static int s_zTotal = 0, s_zHidden = 0;
void LastDepth(int &total, int &hidden) { total = s_zTotal; hidden = s_zHidden; }

// v17: igual, con la prueba de profundidad contra la escena (Target::zbuf, ya en la máscara). Solo para sprites con
// transparencia. v17.0 r2: cuenta los píxeles que se verían y los tapados; DRAW = false solo cuenta (Target::zskip).
template <int FMT, bool DRAW> static void CopyZT(const Target &t, const uint16_t *img, int iw, int X0, int Y0, int xa, int xb, int ya, int yb) {
    const int dx = t.dx, dy = t.dy;
    int total = 0, hidden = 0;
    for (int x = xa; x <= xb; x++) {
        const uint16_t *src = img + (ya - Y0) * iw + (x - X0);
        uint8_t *p = t.base + x * dx + ya * dy;
        const uint32_t *mc = MaskCol(x);
        for (int y = ya; y <= yb; y++, src += iw, p += dy) {
            uint32_t c = *src;
            if (!c) continue;
            total++;
            if (MaskBit(mc, y)) { hidden++; continue; }         // la escena está delante
            if (DRAW) Store<FMT>(p, c);
        }
    }
    s_zTotal = total; s_zHidden = hidden;
}

template <bool KEY> static void Copy(const Target &t, const uint16_t *img, int iw, int ih, int X0, int Y0) {
    int xa = X0 < 0 ? 0 : X0, ya = Y0 < 0 ? 0 : Y0;
    int xb = X0 + iw - 1, yb = Y0 + ih - 1;
    if (xb > t.w - 1) xb = t.w - 1;
    if (yb > t.h - 1) yb = t.h - 1;
    if (xa > xb || ya > yb || !t.base) return;
    if (KEY) s_zTotal = s_zHidden = 0;
    if (KEY && t.zbuf && t.zthr && t.w == 400 && BuildZMask(t, xa, xb, ya, yb)) {
        if (t.zskip) { CopyZT<FB_RGB565, false>(t, img, iw, X0, Y0, xa, xb, ya, yb); return; }   // solo contar
        switch (t.fmt) {
            case FB_RGBA8:  CopyZT<FB_RGBA8, true>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
            case FB_BGR8:   CopyZT<FB_BGR8, true>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
            case FB_RGB565: CopyZT<FB_RGB565, true>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
            case FB_RGB5A1: CopyZT<FB_RGB5A1, true>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
            case FB_RGBA4:  CopyZT<FB_RGBA4, true>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
        }
        return;
    }
    switch (t.fmt) {
        case FB_RGBA8:  CopyT<FB_RGBA8, KEY>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
        case FB_BGR8:   CopyT<FB_BGR8, KEY>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
        case FB_RGB565: CopyT<FB_RGB565, KEY>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
        case FB_RGB5A1: CopyT<FB_RGB5A1, KEY>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
        case FB_RGBA4:  CopyT<FB_RGBA4, KEY>(t, img, iw, X0, Y0, xa, xb, ya, yb); break;
    }
}

uint32_t DepthThreshold(const Mat44 &vp, const Vec3 &feet, float margin) {
    // en una proyección en perspectiva z_clip = a * w_clip + b en toda la imagen: se sacan a y b con dos puntos y se
    // pasa "w de los pies - margen" a la profundidad del juego (-z/w * 0xFFFFFF, comprobado en vivo con DepthProbe)
    float c1[4], c2[4];
    M44Point(vp, feet, c1);
    M44Point(vp, V3(feet.x, feet.y + 100.f, feet.z), c2);
    float dw = c2[3] - c1[3];
    if (!(c1[3] > 1.f) || !(fabsf(dw) > 1e-3f)) return 0;
    float a = (c2[2] - c1[2]) / dw, b = c1[2] - a * c1[3];
    float wt = c1[3] - margin;
    if (!(wt > 1.f)) return 0;
    float nd = -a - b / wt;
    if (!(nd > 0.f) || !(nd < 1.f)) return 0;
    return (uint32_t)(nd * 16777215.f);
}

void Blit565Key(const Target &t, const uint16_t *img, int w, int h, int x0, int y0) {
    if (img && w > 0 && h > 0) Copy<true>(t, img, w, h, x0, y0);
}

void Blit565(const Target &t, const uint16_t *img, int w, int h, int x0, int y0) {
    if (img && w > 0 && h > 0) Copy<false>(t, img, w, h, x0, y0);
}

// Escalado al vecino más cercano (sx, sy en 16.16) con transparencia, espejo horizontal y hasta 2 huecos. v16.3: la fila
// de origen de cada y se calcula una vez (no en cada píxel) y los huecos se saltan de golpe: ~2 veces más rápido (los
// Pokémon acompañantes se copian así en cada cuadro). Mismo resultado bit a bit que la versión anterior (comprobado).
// v17.0 r2: con ZT cuenta los píxeles que se verían (fuera de los huecos) y los que tapa la escena (máscara); DRAW =
// false solo cuenta (Target::zskip).
template <int FMT, bool ZT, bool DRAW> static void ScaleT(const Target &t, const uint16_t *img, int iw, int ih, int X0, int Y0, uint32_t sx,
                                                          uint32_t sy, bool flip, int xa, int xb, int ya, int yb, const int *holes, int nh) {
    int total = 0, hidden = 0;
    static int vrow[256];                              // y - ya -> desplazamiento de su fila en img
    for (int y = ya; y <= yb; y++) {
        int v = (int)(((uint32_t)(y - Y0) * sy) >> 16);
        if (v >= ih) v = ih - 1;
        vrow[y - ya] = v * iw;
    }
    const int dy = t.dy;
    for (int x = xa; x <= xb; x++) {
        int u = (int)(((uint32_t)(x - X0) * sx) >> 16);
        if (u >= iw) u = iw - 1;
        if (flip) u = iw - 1 - u;
        // tramos de esta columna tapados por los huecos (como mucho 2)
        int h0a = 1, h0b = 0, h1a = 1, h1b = 0;
        for (int k = 0; k < nh && holes; k++) {
            const int *h = holes + 4 * k;
            if (x < h[0] || x > h[2]) continue;
            if (h0a > h0b) { h0a = h[1]; h0b = h[3]; } else { h1a = h[1]; h1b = h[3]; }
        }
        const uint16_t *col = img + u;
        uint8_t *p = t.base + x * t.dx + ya * dy;
        const uint32_t *mc = ZT ? MaskCol(x) : nullptr;
        int y = ya;
        while (y <= yb) {
            if (y >= h0a && y <= h0b) { p += (h0b + 1 - y) * dy; y = h0b + 1; continue; }
            if (y >= h1a && y <= h1b) { p += (h1b + 1 - y) * dy; y = h1b + 1; continue; }
            int stop = yb;
            if (h0a <= h0b && h0a > y && h0a - 1 < stop) stop = h0a - 1;
            if (h1a <= h1b && h1a > y && h1a - 1 < stop) stop = h1a - 1;
            const int *vr = vrow + (y - ya);
            for (; y <= stop; y++, p += dy) {
                uint32_t c = col[*vr++];
                if (!c) continue;
                if (ZT) {
                    total++;
                    if (MaskBit(mc, y)) { hidden++; continue; }    // la escena está delante
                }
                if (DRAW) Store<FMT>(p, c);
            }
        }
    }
    if (ZT) { s_zTotal = total; s_zHidden = hidden; }
}

void BlitScaledKey(const Target &t, const uint16_t *img, int iw, int ih, int x0, int y0, int dw, int dh, bool flipX, const int *hole, int nh) {
    if (!hole) nh = 0;
    if (nh > 2) nh = 2;
    s_zTotal = s_zHidden = 0;
    if (!img || iw <= 0 || ih <= 0 || dw <= 0 || dh <= 0 || !t.base) return;
    int xa = x0 < 0 ? 0 : x0, ya = y0 < 0 ? 0 : y0;
    int xb = x0 + dw - 1, yb = y0 + dh - 1;
    if (xb > t.w - 1) xb = t.w - 1;
    if (yb > t.h - 1) yb = t.h - 1;
    if (xa > xb || ya > yb) return;
    uint32_t sx = ((uint32_t)iw << 16) / (uint32_t)dw, sy = ((uint32_t)ih << 16) / (uint32_t)dh;
    if (t.zbuf && t.zthr && t.w == 400 && BuildZMask(t, xa, xb, ya, yb)) {
        if (t.zskip) {                                  // v17.0 r2: casi entero detrás de algo: solo se cuenta
            ScaleT<FB_RGB565, true, false>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh);
            return;
        }
        switch (t.fmt) {
            case FB_RGBA8:  ScaleT<FB_RGBA8, true, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
            case FB_BGR8:   ScaleT<FB_BGR8, true, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
            case FB_RGB565: ScaleT<FB_RGB565, true, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
            case FB_RGB5A1: ScaleT<FB_RGB5A1, true, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
            case FB_RGBA4:  ScaleT<FB_RGBA4, true, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
        }
        return;
    }
    switch (t.fmt) {
        case FB_RGBA8:  ScaleT<FB_RGBA8, false, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
        case FB_BGR8:   ScaleT<FB_BGR8, false, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
        case FB_RGB565: ScaleT<FB_RGB565, false, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
        case FB_RGB5A1: ScaleT<FB_RGB5A1, false, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
        case FB_RGBA4:  ScaleT<FB_RGBA4, false, true>(t, img, iw, ih, x0, y0, sx, sy, flipX, xa, xb, ya, yb, hole, nh); break;
    }
}

// ---------------------------------------------------------------------------------------------
// Construcción de la caché del modelo (una vez; ~300 k instrucciones). Segura ante datos raros: índices
// fuera de rango, huesos inválidos o UV no finitas se descartan o se sustituyen.
static inline uint32_t FBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline uint32_t HashMix(uint32_t h, uint32_t v) { h ^= v; h *= 0x01000193u; h ^= h >> 15; return h; }

// valor * escala redondeado y limitado a [-lim, lim] (NaN -> 0)
static int32_t FixClamp(float v, float scale, float lim) {
    float x = v * scale;
    if (!(x == x)) return 0;
    x = x < 0 ? x - 0.5f : x + 0.5f;
    if (x > lim) x = lim;
    if (x < -lim) x = -lim;
    return (int32_t)x;
}
// parte fraccionaria de v (en [0, 1)) en 2^-30
static int32_t Frac30(float v) {
    if (!(v == v) || fabsf(v) > 1e6f) return 0;
    float f = v - floorf(v);
    int32_t r = (int32_t)(f * 1073741824.f + 0.5f);
    return r < 0 ? 0 : r > 1073741823 ? 1073741823 : r;
}

static bool BuildCache(ModelCache &C, const Hero::Model &M) {
    FreeCache(C);
    const int n = M.vertCount;
    if (n <= 0 || n > 65535 || !M.verts) return false;
    int hsize = 1;
    while (hsize < 2 * n) hsize <<= 1;
    int32_t  *hc = (int32_t *)malloc(hsize * 4), *hv = (int32_t *)malloc(hsize * 4);
    uint16_t *vcombo = (uint16_t *)malloc(n * 2);
    int32_t  *vuniq = (int32_t *)malloc(n * 4), *urep = (int32_t *)malloc(n * 4), *spos = (int32_t *)malloc(n * 4);
    Combo    *combos = (Combo *)malloc(n * sizeof(Combo));
    bool ok = hc && hv && vcombo && vuniq && urep && spos && combos;
    int nC = 0, nU = 0;
    if (ok) {
        memset(hc, 0xFF, hsize * 4);
        memset(hv, 0xFF, hsize * 4);
        // 1) combinación de huesos/pesos de cada vértice (como el skinning original: peso 255 en el primero
        //    = solo ese hueso; si no, todos los pesos no nulos)
        for (int i = 0; i < n; i++) {
            const Hero::Vertex &V = M.verts[i];
            Combo k;
            memset(&k, 0, sizeof(k));
            for (int j = 0; j < 4; j++) {
                if (!V.weight[j] || (j > 0 && V.weight[0] == 255)) continue;
                k.bone[k.n] = V.bone[j] < M.boneCount ? V.bone[j] : 0;
                k.raw[k.n] = V.weight[j];
                k.w[k.n] = V.weight[0] == 255 ? 1.f : V.weight[j] * (1.f / 255.f);
                k.n++;
            }
            uint32_t key0 = k.bone[0] | (k.bone[1] << 8) | (k.bone[2] << 16) | ((uint32_t)k.bone[3] << 24);
            uint32_t key1 = k.raw[0] | (k.raw[1] << 8) | (k.raw[2] << 16) | ((uint32_t)k.raw[3] << 24) ;
            uint32_t h = HashMix(HashMix(0x811C9DC5u, key0), key1 ^ k.n) & (hsize - 1);
            int id = -1;
            while (hc[h] >= 0) {
                const Combo &o = combos[hc[h]];
                if (o.n == k.n && !memcmp(o.bone, k.bone, 4) && !memcmp(o.raw, k.raw, 4)) { id = hc[h]; break; }
                h = (h + 1) & (hsize - 1);
            }
            if (id < 0) { id = nC; combos[nC++] = k; hc[h] = id; }
            vcombo[i] = (uint16_t)id;
        }
        // 2) vértices únicos: misma posición, normal y combinación (solo cambian las UV)
        for (int i = 0; i < n; i++) {
            const Hero::Vertex &V = M.verts[i];
            uint32_t b[7] = { FBits(V.px), FBits(V.py), FBits(V.pz), FBits(V.nx), FBits(V.ny), FBits(V.nz), vcombo[i] };
            uint32_t h = 0x811C9DC5u;
            for (int j = 0; j < 7; j++) h = HashMix(h, b[j]);
            h &= hsize - 1;
            int id = -1;
            while (hv[h] >= 0) {
                const Hero::Vertex &O = M.verts[urep[hv[h]]];
                uint32_t o[7] = { FBits(O.px), FBits(O.py), FBits(O.pz), FBits(O.nx), FBits(O.ny), FBits(O.nz), vcombo[urep[hv[h]]] };
                if (!memcmp(o, b, sizeof(b))) { id = hv[h]; break; }
                h = (h + 1) & (hsize - 1);
            }
            if (id < 0) { id = nU; urep[nU++] = i; hv[h] = id; }
            vuniq[i] = id;
        }
        // 3) ordenar los únicos por combinación (cuenta y prefijos)
        C.ccount = (uint16_t *)malloc(nC * 2);
        C.combos = (Combo *)malloc(nC * sizeof(Combo));
        C.upos = (float *)malloc(nU * 6 * sizeof(float));
        ok = C.ccount && C.combos && C.upos;
    }
    if (ok) {
        memcpy(C.combos, combos, nC * sizeof(Combo));
        memset(C.ccount, 0, nC * 2);
        for (int u = 0; u < nU; u++) C.ccount[vcombo[urep[u]]]++;
        int32_t *start = hc;                                   // reutiliza la tabla (hsize >= nC)
        for (int c = 0, s = 0; c < nC; c++) { start[c] = s; s += C.ccount[c]; }
        for (int u = 0; u < nU; u++) {
            int p = start[vcombo[urep[u]]]++;
            spos[u] = p;
            const Hero::Vertex &V = M.verts[urep[u]];
            float *o = C.upos + p * 6;
            o[0] = V.px; o[1] = V.py; o[2] = V.pz; o[3] = V.nx; o[4] = V.ny; o[5] = V.nz;
        }
        // 4) triángulos por rango, en orden de dibujo, sin los degenerados por construcción
        int maxT = 0;
        for (int r = 0; r < M.rangeCount && r < Hero::MAX_RANGES; r++) maxT += (int)(M.ranges[r].indexCount / 3);
        C.tris = (TriRec *)malloc((maxT > 0 ? maxT : 1) * sizeof(TriRec));
        ok = C.tris != nullptr;
        C.bytes = (uint32_t)(nC * 2 + nC * sizeof(Combo) + nU * 6 * sizeof(float) + (maxT > 0 ? maxT : 1) * sizeof(TriRec));
        int nt = 0, nr = 0, total = 0;
        for (int r = 0; ok && r < M.rangeCount && r < Hero::MAX_RANGES; r++) {
            const Hero::DrawRange &DR = M.ranges[r];
            if (DR.texture >= M.texCount || DR.texture >= Hero::MAX_TEX) continue;
            const Hero::Texture &T = M.tex[DR.texture];
            if (!T.px || !T.w || !T.h || !M.indices) continue;
            uint32_t first = DR.firstIndex, cnt = DR.indexCount;
            if (first > (uint32_t)M.indexCount) continue;
            if (cnt > (uint32_t)M.indexCount - first) cnt = (uint32_t)M.indexCount - first;
            RangeRec &RR = C.ranges[nr];
            RR.first = nt; RR.tex = DR.texture; RR.face = DR.isFace; RR.head = DR.isHead;
            const uint16_t *ix = M.indices + first;
            for (uint32_t k = 0; k + 2 < cnt; k += 3) {
                total++;
                int i0 = ix[k], i1 = ix[k + 2], i2 = ix[k + 1];      // orden de dibujo (A, B, C)
                if (i0 >= n || i1 >= n || i2 >= n) continue;
                int a = spos[vuniq[i0]], b = spos[vuniq[i1]], c = spos[vuniq[i2]];
                if (a == b || a == c || b == c) continue;             // área siempre nula
                const Hero::Vertex &VA = M.verts[i0], &VB = M.verts[i1], &VC = M.verts[i2];
                TriRec &t = C.tris[nt++];
                t.a = (uint32_t)a; t.b = (uint32_t)b; t.c = (uint32_t)c;
                t.u0 = Frac30(VA.u); t.v0 = Frac30(-VA.v);
                int32_t du1 = FixClamp(VB.u - VA.u, 65536.f, 32767.f), du2 = FixClamp(VC.u - VA.u, 65536.f, 32767.f);
                int32_t dv1 = FixClamp(VA.v - VB.v, 65536.f, 32767.f), dv2 = FixClamp(VA.v - VC.v, 65536.f, 32767.f);
                t.du = ((uint32_t)du1 & 0xFFFF) | ((uint32_t)du2 << 16);
                t.dv = ((uint32_t)dv1 & 0xFFFF) | ((uint32_t)dv2 << 16);
            }
            RR.count = nt - RR.first;
            nr++;
        }
        C.nTris = nt; C.nRanges = nr; C.trisTotal = total;
    }
    free(hc); free(hv); free(vcombo); free(vuniq); free(urep); free(spos); free(combos);
    if (!ok) { FreeCache(C); return false; }
    C.model = &M; C.verts = M.verts; C.indices = M.indices;
    C.vertCount = M.vertCount; C.indexCount = M.indexCount; C.rangeCount = M.rangeCount; C.boneCount = M.boneCount;
    C.nU = nU; C.nC = nC;
    return true;
}

static ModelCache *GetCache(const Hero::Model &M) {
    ModelCache *CP = nullptr;
    int freeSlot = -1;
    for (int i = 0; i < MAX_CACHE && !CP; i++) {
        ModelCache &C = s_mc[i];
        if (!C.model) { if (freeSlot < 0) freeSlot = i; continue; }
        if (C.model == &M && C.verts == M.verts && C.indices == M.indices && C.vertCount == M.vertCount &&
            C.indexCount == M.indexCount && C.rangeCount == M.rangeCount && C.boneCount == M.boneCount) CP = &C;
    }
    if (!CP) {
        if (freeSlot < 0) freeSlot = (int)(s_frameNo % MAX_CACHE);
        CP = &s_mc[freeSlot];
        if (!BuildCache(*CP, M)) return nullptr;
    }
    // búferes por cuadro con capacidad para este modelo
    if (s_vCap < CP->nU) {
        free(s_vxy); free(s_vzs);
        s_vxy = (uint32_t *)malloc(CP->nU * 4); s_vzs = (uint32_t *)malloc(CP->nU * 4);
        s_vCap = (s_vxy && s_vzs) ? CP->nU : 0;
        if (!s_vCap) { free(s_vxy); free(s_vzs); s_vxy = s_vzs = nullptr; return nullptr; }
    }
    if (s_cCap < CP->nC) {
        free(s_cbuf); free(s_cm);
        s_cbuf = (BoneXf *)malloc(CP->nC * sizeof(BoneXf)); s_cm = (const float **)malloc(CP->nC * sizeof(float *));
        s_cCap = (s_cbuf && s_cm) ? CP->nC : 0;
        if (!s_cCap) { free(s_cbuf); free(s_cm); s_cbuf = nullptr; s_cm = nullptr; return nullptr; }
    }
    return CP;
}

bool Prewarm(const Hero::Model &M) {
    if (!s_inited) Init();
    return M.ok && GetCache(M) != nullptr;
}

uint32_t CacheBytes() {
    uint32_t n = 0;
    for (int i = 0; i < MAX_CACHE; i++) if (s_mc[i].model) n += s_mc[i].bytes;
    return n;
}

// ---------------------------------------------------------------------------------------------
static void BlendMat(Mat34 &o, const Mat34 &a, const Mat34 &b, float t) {
    for (int i = 0; i < 12; i++) o.m[i] = a.m[i] + (b.m[i] - a.m[i]) * t;
}

// Matrices de piel del instante actual (interpolando entre cuadros horneados)
static void PoseSkin(const Hero::Model &M, int anim, float frame, Mat34 *out) {
    int N = (anim >= 0 && anim < Hero::ANIM_COUNT) ? M.animFrames[anim] : 0;
    if (N <= 0 || !M.animSkin[anim]) {
        for (int b = 0; b < M.boneCount; b++) M34Identity(out[b]);
        if (anim >= 0 && anim < Hero::ANIM_COUNT && M.animSkin[anim])
            for (int b = 0; b < M.boneCount; b++) out[b] = M.animSkin[anim][b];
        return;
    }
    float f = fmodf(frame, (float)N);
    if (f < 0) f += N;
    if (!(f >= 0.f && f < (float)N)) f = 0.f;          // NaN o límite numérico
    int f0 = (int)f, f1 = f0 + 1;
    if (f0 >= N) f0 = N - 1;
    if (f1 > N) f1 = N;                                // el cuadro N existe (vuelta al inicio horneada)
    float t = f - f0;
    const Mat34 *A = M.animSkin[anim] + f0 * M.boneCount, *B = M.animSkin[anim] + f1 * M.boneCount;
    if (t < 0.001f) { for (int b = 0; b < M.boneCount; b++) out[b] = A[b]; return; }
    for (int b = 0; b < M.boneCount; b++) BlendMat(out[b], A[b], B[b], t);
}

static inline bool ProjectPt(const Mat44 &vp, const Vec3 &p, float &sx, float &sy, float &w) {
    float c[4];
    M44Point(vp, p, c);
    if (!(c[3] > 1.0f)) return false;
    float iw = 1.0f / c[3];
    sx = 200.f + 200.f * c[0] * iw;
    sy = 120.f - 120.f * c[1] * iw;
    w = c[3];
    return sx == sx && sy == sy && fabsf(sx) < 1e6f && fabsf(sy) < 1e6f;
}

// Pies, cabeza, derecha y frente en pantalla: si no cambian (salvo traslación), el sprite sirve.
static ViewSig MakeSig(const Instance &in, const Mat44 &vp) {
    ViewSig s;
    memset(&s, 0, sizeof(s));
    float cy = cosf(in.yaw), sy = sinf(in.yaw), w;
    float h = in.model->height > 1.f ? in.model->height : 33.5f;
    if (!ProjectPt(vp, V3(in.pos.x, in.pos.z, in.pos.y), s.fx, s.fy, s.fw) || s.fw < 2.f) return s;
    float x, y;
    if (!ProjectPt(vp, V3(in.pos.x, in.pos.z + h, in.pos.y), x, y, w)) return s;
    s.hx = x - s.fx; s.hy = y - s.fy;
    if (!ProjectPt(vp, V3(in.pos.x + 12.f * cy, in.pos.z, in.pos.y - 12.f * sy), x, y, w)) return s;
    s.rx = x - s.fx; s.ry = y - s.fy;
    if (!ProjectPt(vp, V3(in.pos.x + 12.f * sy, in.pos.z, in.pos.y + 12.f * cy), x, y, w)) return s;
    s.gx = x - s.fx; s.gy = y - s.fy;
    s.ok = true;
    return s;
}

static inline bool Near(float a, float b, float tol) { float d = a - b; return d < tol && d > -tol; }

static bool FrameNear(float a, float b, int N) {
    float d = fabsf(a - b);
    if (N > 0 && d > N * 0.5f) d = N - d;               // ciclo
    return d < 0.2f;
}

static bool NeedsRender(const Sprite &S, const Instance &in, const ViewSig &g) {
    if (!S.valid || S.model != in.model || S.attach != in.attach || S.anim != in.anim || S.blink != in.blink || S.variant != in.variant || S.acc != in.acc) return true;
    const Hero::Model &M = *in.model;
    if (!FrameNear(S.frame, in.frame, M.animFrames[in.anim])) return true;
    bool prevNow = in.prevAnim >= 0 && in.blend < 0.999f, prevThen = S.prevAnim >= 0;
    if (prevNow != prevThen) return true;
    if (prevNow) {
        if (S.prevAnim != in.prevAnim || !Near(S.blend, in.blend, 0.05f)) return true;
        if (in.prevAnim < Hero::ANIM_COUNT && !FrameNear(S.prevFrame, in.prevFrame, M.animFrames[in.prevAnim])) return true;
    }
    return !(Near(S.hx, g.hx, 0.45f) && Near(S.hy, g.hy, 0.45f) && Near(S.rx, g.rx, 0.35f) &&
             Near(S.ry, g.ry, 0.35f) && Near(S.gx, g.gx, 0.35f) && Near(S.gy, g.gy, 0.35f));
}

// ---------------------------------------------------------------------------------------------
// Vértices únicos -> XY (28.4 con sesgo, 0 = inválido) y ZS (z 8.8 | luz << 16). Devuelve la caja
// empaquetada de los válidos (mn > mx en x si no hay ninguno).
// v16: capacidad para los vértices de dos modelos a la vez (personaje + bici)
static bool EnsureVerts(int n) {
    if (s_vCap >= n) return true;
    free(s_vxy); free(s_vzs);
    s_vxy = (uint32_t *)malloc(n * 4); s_vzs = (uint32_t *)malloc(n * 4);
    s_vCap = (s_vxy && s_vzs) ? n : 0;
    if (!s_vCap) { free(s_vxy); free(s_vzs); s_vxy = s_vzs = nullptr; return false; }
    return true;
}

__attribute__((noinline)) static void VertexPass(const ModelCache &C, const float *const *cm, float zb256, int32_t zthr,
                       uint32_t *oxy, uint32_t *ozs, uint32_t &mnOut, uint32_t &mxOut) {
    uint32_t mn = 0x7FFF7FFFu, mx = 0;
    const float *P = C.upos;
    const uint32_t *lut = s_shadeLut;
    for (int c = 0; c < C.nC; c++) {
        const float *m = cm[c];
        const float m0 = m[0], m1 = m[1], m2 = m[2], m3 = m[3], m4 = m[4], m5 = m[5], m6 = m[6], m7 = m[7];
        const float m8 = m[8], m9 = m[9], m10 = m[10], m11 = m[11], m12 = m[12], m13 = m[13], m14 = m[14], m15 = m[15];
        const float *pe = P + 6 * C.ccount[c];
        for (; P < pe; P += 6) {
            const float px = P[0], py = P[1], pz = P[2];
            const float X = m0 * px + m1 * py + m2 * pz + m3;
            const float Y = m4 * px + m5 * py + m6 * pz + m7;
            const float Wf = m8 * px + m9 * py + m10 * pz + m11;
            const float Lf = m12 * P[3] + m13 * P[4] + m14 * P[5] + m15;
            const int32_t zi = CvtSat(Wf * 256.f - zb256);
            const int32_t xi = CvtSat(X / Wf), yi = CvtSat(Y / Wf);
            const int32_t li = CvtSat(Lf);
            uint32_t xy = 0;
            if (zi > zthr && (uint32_t)(xi - VMIN) < (uint32_t)(VMAX - VMIN) && (uint32_t)(yi - VMIN) < (uint32_t)(VMAX - VMIN)) {
                xy = (uint32_t)xi | ((uint32_t)yi << 16);
                MinMax16(mn, mx, xy);
            }
            *oxy++ = xy;
            *ozs++ = Usat16(zi) | (lut[Usat8(li)] << 16);
        }
    }
    mnOut = mn; mxOut = mx;
}

// Contexto de un rango de triángulos
struct RCtx {
    const uint32_t *xy, *zs;
    uint16_t *px, *zb;
    int W, H, OX, OY;                          // sprite y su origen (píxel con sesgo)
    const uint16_t *tex;
    uint32_t TW, TH;
    int sU, sV;                                // textura potencia de 2: desplazamientos y máscaras del
    uint32_t mU, mV;                           // desplazamiento en bytes del texel
    int32_t uShift;                            // parpadeo (u + 0.25)
    uint32_t variant;
    int nDrawn, nPix;
};

// Color final del píxel (RGB565, nunca 0) desde el texel RGBA5551 y la luz (0..256)
// variant = color 1..15 de SkinColors (+ EFF_WHITE: también el blanco, el gorro de Bruno)
template <bool VAR> static inline uint32_t Shade(uint32_t texel, uint32_t sh, uint32_t variant) {
    uint32_t R5 = texel >> 11, G5 = (texel >> 6) & 31, B5 = (texel >> 1) & 31;
    if (VAR) {
        bool hit = R5 >= 12 && G5 * 2 <= R5 && B5 * 2 <= R5 + 2;      // rojo intenso de la ropa
        if (!hit && (variant & SkinColors::EFF_WHITE)) {               // v16.5: blanco o gris claro (gorro)
            uint32_t mn = R5 < G5 ? R5 : G5, mx = R5 > G5 ? R5 : G5;
            if (B5 < mn) mn = B5;
            if (B5 > mx) mx = B5;
            hit = mn >= 16 && mx - mn <= 5;
        }
        if (hit) {
            // el nuevo color conserva el sombreado de la tela (intensidad del rojo original)
            const uint8_t *k = SkinColors::kTint[variant & 15];
            uint32_t I = R5;
            R5 = (k[0] * I * 33) >> 10; G5 = (k[1] * I * 33) >> 10; B5 = (k[2] * I * 33) >> 10;   // x/31 sin división
        }
    }
    uint32_t G6 = (G5 << 1) | (G5 >> 4);
    uint32_t out = (((R5 * sh) >> 8) << 11) | (((G6 * sh) >> 8) << 5) | ((B5 * sh) >> 8);
    return out ? out : 0x0821;
}

// Píxel cubierto: z, textura, alfa, sombreado. l1/l2 = pesos de B y C ya multiplicados por el recíproco.
struct TriAttr { uint32_t DZ, DS, DU, DV; int32_t Zacc, Sacc, Uacc, Vacc; };

// Rasteriza los triángulos [t, te). CLIP: el sprite está recortado (hay que recortar cada caja);
// POT: textura potencia de 2 (máscaras); VAR: variante de color.
// Cobertura: con f = ~e (e = función de borde con su sesgo de relleno) un centro está dentro si los tres f
// son negativos, y cada f se evalúa directamente en el centro Q (empaquetado x | y << 16) con una sola
// instrucción: f = K + Q.x * E.y - Q.y * E.x. Rangos: |E| < 4096 (caja < SPAN_MAX), Q < 32768, así que
// cada producto < 2^27 y f cabe de sobra en int32. La preparación de atributos (recíproco del área, z,
// luz, UV) se hace solo al encontrar el primer centro cubierto: la mitad de los triángulos que pasan la
// caja no cubre ninguno.
template <bool CLIP, bool POT, bool VAR>
__attribute__((noinline)) static void RasterTris(RCtx &R, const TriRec *t, const TriRec *te) {
    const uint32_t *const XY = R.xy, *const ZS = R.zs;
    uint16_t *const px = R.px;
    uint16_t *const zb = R.zb;
    const int W = R.W;
    const int32_t OFF = R.OY * R.W + R.OX;
    const uint8_t *const tex = (const uint8_t *)R.tex;
    const uint32_t CW = 1u | (4096u << 16);
    const uint32_t LO = (uint32_t)(R.OX * 16) | ((uint32_t)(R.OY * 16) << 16);
    const uint32_t HI = (uint32_t)((R.OX + W - 1) * 16) | ((uint32_t)((R.OY + R.H - 1) * 16) << 16);
    int nDrawn = 0, nPix = 0;
    for (; t < te; t++) {
        const uint32_t ia = t->a, ib = t->b, ic = t->c;
        const uint32_t A = XY[ia], B = XY[ib], C = XY[ic];
        const uint32_t dB = Sub16(B, A), dC = Sub16(C, A);
        const int32_t area = Cross16(dB, dC);
        if (area <= 0) continue;                       // cara trasera o degenerada
        uint32_t mn = A, mx = A;
        MinMax16(mn, mx, B);
        MinMax16(mn, mx, C);
        const uint32_t span = Sub16(mx, mn);
        if ((span | (span << 16)) >= ((uint32_t)SPAN_MAX << 16)) continue;   // enorme o con un vértice inválido
        // primer y último centro de píxel (16 i + 8) dentro de la caja, como múltiplos de 16 (sin acarreos:
        // las coordenadas válidas están en [VMIN, VMAX))
        uint32_t c0 = (mn + 0x00070007u) & 0xFFF0FFF0u, c1 = (mx - 0x00080008u) & 0xFFF0FFF0u;
        if (CLIP) {
            uint32_t a0 = c0, b0 = c0, a1 = c1, b1 = c1;
            MinMax16(a0, b0, LO); c0 = b0;             // max(c0, LO)
            MinMax16(a1, b1, HI); c1 = a1;             // min(c1, HI)
        }
        const uint32_t dd = Sub16(c1, c0);
        if ((dd | (dd << 16)) & 0x80000000u) continue; // no cubre ningún centro de píxel
        nDrawn++;
        const uint32_t E0 = Sub16(C, B), E1 = Sub16(A, C), E2 = dB;
        // K = cross(E, V) + (sesgo de relleno superior-izquierdo: -1 si 4096*dy - dx <= 0)
        const int32_t K0 = Cross16(E0, B) + (DotSub16(E0, CW) >> 31);
        const int32_t K1 = Cross16(E1, C) + (DotSub16(E1, CW) >> 31);
        const int32_t K2 = Cross16(E2, A) + (DotSub16(E2, CW) >> 31);
        uint32_t Qrow = c0 + 0x00080008u;
        const uint32_t Qlast = Qrow + (dd & 0xFFFF0000u), xs = dd & 0xFFFFu;
        uint32_t Q = Qrow, Qxe = Qrow + xs;
        int32_t f1, f2;
        // 1) buscar el primer centro cubierto
        for (;;) {
            const int32_t g0 = CrossAcc16(Q, E0, K0);
            f1 = CrossAcc16(Q, E1, K1);
            f2 = CrossAcc16(Q, E2, K2);
            if ((g0 & f1 & f2) < 0) break;
            if (Q != Qxe) { Q += 16; continue; }
            if (Qrow == Qlast) goto nextTri;
            Qrow += 0x00100000u; Q = Qrow; Qxe = Qrow + xs;
        }
        {
            // 2) atributos del triángulo: l = e * inv >> 16 en 2^-14 (e <= área -> sin desborde)
            const int32_t ninv = (int32_t)(-1073741824.f / (float)area);         // área en [1, 2^25)
            const uint32_t zA = ZS[ia], zB = ZS[ib], zC = ZS[ic];
            const uint32_t d1 = Sub16(zB, zA), d2 = Sub16(zC, zA);
            const uint32_t DZ = (d1 & 0xFFFF) | (d2 << 16), DS = (d1 >> 16) | (d2 & 0xFFFF0000u);
            const int32_t Zacc = (int32_t)((zA & 0xFFFF) << 14), Sacc = (int32_t)((zA >> 16) << 14);
            const int32_t Uacc = t->u0 + R.uShift, Vacc = t->v0;
            const uint32_t DU = t->du, DV = t->dv;
            for (;;) {
                {
                    // (~f) * inv = f * ninv + ninv
                    const uint32_t q1 = (uint32_t)f1 * (uint32_t)ninv + (uint32_t)ninv;
                    const uint32_t q2 = (uint32_t)f2 * (uint32_t)ninv + (uint32_t)ninv;
                    const uint32_t L = PackHiHi(q2, q1);
                    const int32_t z = Dot16(L, DZ, Zacc);
                    const int idx = (int)(Q >> 20) * W + (int)((Q & 0xFFFF) >> 4) - OFF;
                    if ((z >> 14) < (int32_t)zb[idx]) {
                        const int32_t U = Dot16(L, DU, Uacc), V = Dot16(L, DV, Vacc);
                        uint32_t texel;
                        if (POT) {
                            texel = *(const uint16_t *)(tex + ((((uint32_t)(U >> R.sU)) & R.mU) + (((uint32_t)(V >> R.sV)) & R.mV)));
                        } else {
                            const uint32_t tx = ((((uint32_t)U >> 14) & 0xFFFF) * R.TW) >> 16;
                            const uint32_t ty = ((((uint32_t)V >> 14) & 0xFFFF) * R.TH) >> 16;
                            texel = ((const uint16_t *)tex)[ty * R.TW + tx];
                        }
                        if (texel & 1) {                       // alfa de 1 bit
                            const uint32_t sh = (uint32_t)(Dot16(L, DS, Sacc) >> 14);
                            px[idx] = (uint16_t)Shade<VAR>(texel, sh, R.variant);
                            zb[idx] = (uint16_t)(z >> 14);
                            nPix++;
                        }
                    }
                }
                // 3) siguientes centros
                for (;;) {
                    if (Q != Qxe) Q += 16;
                    else if (Qrow == Qlast) goto nextTri;
                    else { Qrow += 0x00100000u; Q = Qrow; Qxe = Qrow + xs; }
                    const int32_t g0 = CrossAcc16(Q, E0, K0);
                    f1 = CrossAcc16(Q, E1, K1);
                    f2 = CrossAcc16(Q, E2, K2);
                    if ((g0 & f1 & f2) < 0) break;
                }
            }
        }
    nextTri:;
    }
    R.nDrawn += nDrawn; R.nPix += nPix;
}

template <bool CLIP, bool POT>
static void RasterTrisV(RCtx &R, const TriRec *t, const TriRec *te) {
    if (R.variant) RasterTris<CLIP, POT, true>(R, t, te);
    else RasterTris<CLIP, POT, false>(R, t, te);
}

// Contorno de 1 px dentro de la silueta (estilo del juego)
static inline uint16_t Darken(uint32_t c) {
    uint32_t r5 = ((c >> 11) * 90 >> 8) + 2, g6 = (((c >> 5) & 63) * 90 >> 8) + 3, b5 = ((c & 31) * 90 >> 8) + 2;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

__attribute__((noinline)) static void Outline(uint16_t *px, int W, int H, bool border) {
    if (!border) {                                     // el borde del sprite está vacío por construcción
        for (int y = 1; y < H - 1; y++) {
            uint16_t *row = px + y * W;
            for (int x = 1; x < W - 1; x++) {
                uint32_t c = row[x];
                if (!c) continue;
                if (row[x - 1] && row[x + 1] && row[x - W] && row[x + W]) continue;
                row[x] = Darken(c);
            }
        }
        return;
    }
    for (int y = 0; y < H; y++) {
        uint16_t *row = px + y * W;
        for (int x = 0; x < W; x++) {
            uint32_t c = row[x];
            if (!c) continue;
            bool edge = x == 0 || y == 0 || x == W - 1 || y == H - 1 || !row[x - 1] || !row[x + 1] || !row[x - W] || !row[x + W];
            if (!edge) continue;
            row[x] = Darken(c);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Rasteriza el avatar en su sprite. false si no se pudo (nada visible, demasiado grande, sin memoria).
// Pose de un modelo -> matrices por combinación de huesos (cm) listas para VertexPass
static void PoseToCombos(const Hero::Model &M, const ModelCache &C, const Instance &in, const float PR[3][4], const Vec3 &Lm,
                         const float **cm) {
    // Pose (con mezcla entre animaciones)
    static Mat34 skin[Hero::MAX_BONES], prev[Hero::MAX_BONES];   // estáticas: pila pequeña en el hilo del juego
    PoseSkin(M, in.anim, in.frame, skin);
    if (in.prevAnim >= 0 && in.prevAnim < Hero::ANIM_COUNT && in.blend < 0.999f) {
        PoseSkin(M, in.prevAnim, in.prevFrame, prev);
        float bt = in.blend < 0 ? 0 : in.blend;
        for (int b = 0; b < M.boneCount; b++) BlendMat(skin[b], prev[b], skin[b], bt);
    }
    // Por hueso: filas X', Y', W de PR * piel, y la luz llevada al espacio del hueso (escalada a índice de tabla)
    static BoneXf bx[Hero::MAX_BONES];
    for (int b = 0; b < M.boneCount; b++) {
        const float *s = skin[b].m;
        BoneXf &o = bx[b];
        for (int r = 0; r < 3; r++)
            for (int j = 0; j < 4; j++)
                o.m[r * 4 + j] = PR[r][0] * s[j] + PR[r][1] * s[4 + j] + PR[r][2] * s[8 + j] + (j == 3 ? PR[r][3] : 0.f);
        float lx = s[0] * Lm.x + s[4] * Lm.y + s[8] * Lm.z;
        float ly = s[1] * Lm.x + s[5] * Lm.y + s[9] * Lm.z;
        float lz = s[2] * Lm.x + s[6] * Lm.y + s[10] * Lm.z;
        float sc = sqrtf(s[0] * s[0] + s[4] * s[4] + s[8] * s[8]);
        float k = sc > 1e-6f ? 64.f / (sc * sc) : 64.f;           // n' = S n tiene longitud sc
        o.m[12] = lx * k; o.m[13] = ly * k; o.m[14] = lz * k; o.m[15] = 64.f;
    }
    // Una matriz por combinación de huesos (mezcla lineal: idéntica a mezclar los vértices transformados)
    for (int c = 0; c < C.nC; c++) {
        const Combo &K = C.combos[c];
        if (K.n == 1 && K.w[0] == 1.f) { cm[c] = bx[K.bone[0]].m; continue; }
        float *o = s_cbuf[c].m;
        if (!K.n) { memset(o, 0, sizeof(BoneXf)); cm[c] = o; continue; }   // sin pesos: queda detrás (w = 0)
        const float *a = bx[K.bone[0]].m;
        const float f0 = K.w[0];
        for (int i = 0; i < 16; i++) o[i] = a[i] * f0;
        for (int j = 1; j < K.n; j++) {
            const float *b = bx[K.bone[j]].m;
            const float f = K.w[j];
            for (int i = 0; i < 16; i++) o[i] += b[i] * f;
        }
        cm[c] = o;
    }

}

// Rangos de triángulos de un modelo sobre el sprite en curso (R.xy/R.zs ya apuntan a SUS vértices)
// variant = color de la ropa; headV = el de la textura de la cabeza (gorro / lazo, SkinColors::HeadEff)
static void RasterModel(RCtx &R, const Hero::Model &M, const ModelCache &C, const Instance &in, uint32_t variant, uint32_t headV, bool clip) {
    for (int r = 0; r < C.nRanges; r++) {
        const RangeRec &RR = C.ranges[r];
        R.variant = RR.face ? 0 : RR.head ? headV : variant;       // la cara (boca roja) nunca se recolorea
        if (RR.tex >= M.texCount) continue;
        const Hero::Texture &T = M.tex[RR.tex];
        if (!T.px || !T.w || !T.h) continue;
        R.tex = T.px; R.TW = T.w; R.TH = T.h;
        R.uShift = (RR.face && in.blink) ? (1 << 28) : 0;          // u + 0.25 en 2^-30
        int lw = 0, lh = 0;
        while ((1u << lw) < R.TW) lw++;
        while ((1u << lh) < R.TH) lh++;
        const bool pot = (1u << lw) == R.TW && (1u << lh) == R.TH && lw + lh <= 24;
        R.sU = 29 - lw; R.mU = (R.TW - 1) << 1;
        R.sV = 29 - lw - lh; R.mV = (R.TH - 1) << (lw + 1);
        const TriRec *t = C.tris + RR.first, *te = t + RR.count;
        if (clip) { if (pot) RasterTrisV<true, true>(R, t, te); else RasterTrisV<true, false>(R, t, te); }
        else      { if (pot) RasterTrisV<false, true>(R, t, te); else RasterTrisV<false, false>(R, t, te); }
    }
}

static bool Render(Sprite &S, const Instance &in, const Mat44 &vp, const ViewSig &g) {
    const Hero::Model &M = *in.model;
    S.valid = false;
    if (M.vertCount <= 0 || M.boneCount <= 0 || M.boneCount > Hero::MAX_BONES) return false;
    // v16: la bici (si la hay) va en el MISMO sprite y z-búfer: se tapan bien entre sí
    const Hero::Model *BM = (in.attach && in.attach->ok && in.attach->vertCount > 0 && in.attach->boneCount > 0 &&
                             in.attach->boneCount <= Hero::MAX_BONES) ? in.attach : nullptr;
    ModelCache *CBP = BM ? GetCache(*BM) : nullptr;     // GetCache también reserva las combinaciones de huesos
    if (!CBP) BM = nullptr;
    ModelCache *CP = GetCache(M);
    if (!CP) { g_stats.noCache++; return false; }
    if (BM && (CBP->model != BM || CP == CBP)) { BM = nullptr; CBP = nullptr; }   // la caché rotó: sin bici este cuadro
    const ModelCache &C = *CP;
    if (BM && !EnsureVerts(C.nU + CBP->nU)) return false;

    // Modelo -> mundo: giro en Y (el modelo mira a +Z) y traslación. Mundo: X este, Y altura, Z sur.
    float cy = cosf(in.yaw), sy = sinf(in.yaw);
    Mat34 world = {{ cy, 0, sy, in.pos.x,
                     0,  1, 0,  in.pos.z,
                     -sy, 0, cy, in.pos.y }};
    Mat44 W4, VW;
    M44FromM34(W4, world);
    M44Mul(VW, vp, W4);
    Vec3 L = Normalize(kLight);
    Vec3 Lm = V3(cy * L.x - sy * L.z, L.y, sy * L.x + cy * L.z);     // luz en el espacio del modelo

    // Filas de proyección ya escaladas a 28.4 con sesgo: X' = 3200 x + 16384 w, Y' = -1920 y + 16384 w
    float PR[3][4];
    for (int j = 0; j < 4; j++) {
        PR[0][j] = 3200.f * VW.m[j] + 16.f * (200 + XB) * VW.m[12 + j];
        PR[1][j] = -1920.f * VW.m[4 + j] + 16.f * (120 + YB) * VW.m[12 + j];
        PR[2][j] = VW.m[12 + j];
    }
    // Vértices: personaje y, a continuación en el mismo búfer, la bici. La caja del sprite es la unión.
    const float zb256 = (g.fw - 96.f) * 256.f;
    const int32_t zthr = CvtSat(256.f - zb256);         // w > 1  <=>  256 w - zb256 > zthr
    uint32_t mn, mx;
    PoseToCombos(M, C, in, PR, Lm, s_cm);
    VertexPass(C, s_cm, zb256, zthr, s_vxy, s_vzs, mn, mx);
    if (BM) {
        uint32_t bmn, bmx;
        PoseToCombos(*BM, *CBP, in, PR, Lm, s_cm);
        VertexPass(*CBP, s_cm, zb256, zthr, s_vxy + C.nU, s_vzs + C.nU, bmn, bmx);
        if ((bmn & 0xFFFF) <= (bmx & 0xFFFF)) {
            if ((mn & 0xFFFF) > (mx & 0xFFFF)) { mn = bmn; mx = bmx; }
            else { MinMax16(mn, mx, bmn); MinMax16(mn, mx, bmx); }
        }
    }
    if ((mn & 0xFFFF) > (mx & 0xFFFF)) return false;

    // Rectángulo del sprite (1 px extra para el contorno), limitado a la pantalla + margen
    int x0 = (int)((mn & 0xFFFF) >> 4) - 1 - XB, y0 = (int)((mn >> 16) >> 4) - 1 - YB;
    int x1 = (int)((mx & 0xFFFF) >> 4) + 2 - XB, y1 = (int)((mx >> 16) >> 4) + 2 - YB;
    const bool clip = x0 < -MARGIN || y0 < -MARGIN || x1 > SW - 1 + MARGIN || y1 > SH - 1 + MARGIN;
    if (x0 < -MARGIN) x0 = -MARGIN;
    if (y0 < -MARGIN) y0 = -MARGIN;
    if (x1 > SW - 1 + MARGIN) x1 = SW - 1 + MARGIN;
    if (y1 > SH - 1 + MARGIN) y1 = SH - 1 + MARGIN;
    if (x1 < x0 || y1 < y0) return false;
    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    if (W * H > MAX_SPR_PIX) { g_stats.tooBig++; return false; }
    if (S.cap < W * H) {
        int want = W * H + W * H / 4;
        if (want > MAX_SPR_PIX) want = MAX_SPR_PIX;
        free(S.px);                                     // primero se suelta el viejo: así cabe en su propio hueco
        S.px = (uint16_t *)malloc(want * 2); S.cap = S.px ? want : 0;
        if (!S.px) { S.valid = false; g_stats.noMemory++; return false; }
    }
    if (s_zCap < W * H) {
        int want = W * H + W * H / 4;
        if (want > MAX_SPR_PIX) want = MAX_SPR_PIX;
        free(s_z);
        s_z = (uint16_t *)malloc(want * 2); s_zCap = s_z ? want : 0;
        if (!s_z) { g_stats.noMemory++; return false; }
    }
    uint16_t *px = S.px;
    memset(px, 0, W * H * 2);
    memset(s_z, 0xFF, W * H * 2);

    // Triángulos
    RCtx R;
    R.xy = s_vxy; R.zs = s_vzs; R.px = px; R.zb = s_z;
    R.W = W; R.H = H; R.OX = x0 + XB; R.OY = y0 + YB;
    const uint32_t variant = in.variant < SkinColors::COUNT ? in.variant : 0;
    const uint32_t headV = SkinColors::HeadEff(variant, in.acc, M.male);
    R.nDrawn = R.nPix = 0;
    RasterModel(R, M, C, in, variant, headV, clip);
    if (BM) {                                                       // la bici: con su color original
        R.xy = s_vxy + C.nU; R.zs = s_vzs + C.nU;
        RasterModel(R, *BM, *CBP, in, 0, 0, clip);
        g_stats.trisIn += CBP->trisTotal;
    }
    g_stats.trisIn += C.trisTotal; g_stats.trisDrawn += R.nDrawn; g_stats.pixels += R.nPix;

    Outline(px, W, H, clip);

    // Punto sobre la cabeza (de la animación real)
    int N = M.animFrames[in.anim];
    int f = 0;
    if (N > 0) { float ff = fmodf(in.frame, (float)N); if (ff < 0) ff += N; f = (ff >= 0.f && ff < N) ? (int)ff : 0; }
    float hx = (x0 + x1) * 0.5f, hy = (float)y0, hw;
    if (M.animHead[in.anim]) {
        Vec3 h = M.animHead[in.anim][f];
        float tx, ty;
        if (ProjectPt(vp, M34Point(world, V3(h.x, h.y + 6.0f, h.z)), tx, ty, hw)) { hx = tx; hy = ty; }
    }

    S.valid = true;
    S.x0 = x0; S.y0 = y0; S.w = W; S.h = H;
    S.model = in.model; S.attach = BM; S.anim = in.anim; S.frame = in.frame;
    S.prevAnim = (in.prevAnim >= 0 && in.blend < 0.999f) ? in.prevAnim : -1;
    S.prevFrame = in.prevFrame; S.blend = in.blend; S.blink = in.blink; S.variant = in.variant; S.acc = in.acc;
    S.fx = g.fx; S.fy = g.fy;
    S.hx = g.hx; S.hy = g.hy; S.rx = g.rx; S.ry = g.ry; S.gx = g.gx; S.gy = g.gy;
    S.headX = hx; S.headY = hy;
    S.lastRender = s_frameNo;
    return true;
}

// ---------------------------------------------------------------------------------------------
void DrawAll(const Target &t, const Instance *inst, int n, const Mat44 &vp, DrawInfo *info) {
    s_frameNo++;
    if (!s_inited) Init();                             // tras ReleaseMemory() (menú)
    if (n > MAX_SLOTS) n = MAX_SLOTS;
    ViewSig sig[MAX_SLOTS];
    bool vis[MAX_SLOTS], need[MAX_SLOTS], done[MAX_SLOTS];
    for (int k = 0; k < n; k++) {
        memset(&info[k], 0, sizeof(DrawInfo));
        vis[k] = need[k] = done[k] = false;
        g_stats.calls++;
        const Instance &in = inst[k];
        if (!in.model || !in.model->ok || in.id == 0 || in.id > MAX_SLOTS ||
            in.anim < 0 || in.anim >= Hero::ANIM_COUNT) { g_stats.noModel++; continue; }
        sig[k] = MakeSig(in, vp);
        if (!sig[k].ok) { g_stats.behind++; continue; }
        float rad = fabsf(sig[k].hx) + fabsf(sig[k].hy) + 24.f;
        if (sig[k].fx < -rad || sig[k].fx > SW + rad || sig[k].fy < -rad || sig[k].fy > SH + 2 * rad) { g_stats.offscreen++; continue; }
        vis[k] = true;
        need[k] = NeedsRender(s_spr[in.id], in, sig[k]);
    }

    // Orden de re-rasterizado: primero los que no tienen sprite, luego los más atrasados
    int order[MAX_SLOTS], m = 0;
    for (int k = 0; k < n; k++) if (vis[k] && need[k]) order[m++] = k;
    for (int a = 1; a < m; a++) {
        int k = order[a], j = a - 1;
        const Sprite &Sk = s_spr[inst[k].id];
        uint32_t pk = inst[k].urgent ? 0xFFFFFFFFu : Sk.valid ? s_frameNo - Sk.lastRender : 0xFFFFFFFEu;
        while (j >= 0) {
            const Sprite &Sj = s_spr[inst[order[j]].id];
            uint32_t pj = inst[order[j]].urgent ? 0xFFFFFFFFu : Sj.valid ? s_frameNo - Sj.lastRender : 0xFFFFFFFEu;
            if (pj >= pk) break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }
    uint64_t spent = 0;
    int renders = 0;
    for (int q = 0; q < m; q++) {
        int k = order[q];
        Sprite &S = s_spr[inst[k].id];
        // pose nueva como mucho cada 2 cuadros (la posición sí se actualiza en todos): la mitad de CPU
        if (s_clock && !inst[k].urgent && S.valid && s_frameNo - S.lastRender < 2) { g_stats.skippedBudget++; continue; }
        if (s_clock && !inst[k].urgent) {
            float est = S.cost ? (float)S.cost : 350000.f;
            if (renders > 0) {
                if ((float)spent + est > s_budget) { g_stats.skippedBudget++; continue; }
            } else if (S.valid && est > 2.f * s_budget && s_frameNo - S.lastRender < 4) {
                // avatar pegado a la cámara (muy caro): como mucho cada 4 cuadros para no bajar los FPS
                g_stats.skippedBudget++;
                continue;
            }
        }
        uint64_t t0 = s_clock ? s_clock() : 0;
        Render(S, inst[k], vp, sig[k]);
        if (s_clock) { uint64_t d = s_clock() - t0; S.cost = (uint32_t)d; spent += d; }
        renders++;
        done[k] = true;
        g_stats.renders++;
    }
    s_spentLast = spent;
    g_stats.ticksRender += spent;

    // Composición de lejos a cerca
    uint64_t c0 = s_clock ? s_clock() : 0;
    for (int k = 0; k < n; k++) {
        if (!vis[k]) continue;
        const Sprite &S = s_spr[inst[k].id];
        if (!S.valid) continue;
        if (!done[k]) g_stats.reuses++;
        int ox = (int)floorf(sig[k].fx - S.fx + 0.5f), oy = (int)floorf(sig[k].fy - S.fy + 0.5f);
        int X0 = S.x0 + ox, Y0 = S.y0 + oy;
        if (X0 + S.w <= 0 || Y0 + S.h <= 0 || X0 >= t.w || Y0 >= t.h) continue;
        if (t.zbuf) {                                   // v17: tapado por lo que tenga delante en la escena
            Target tz = t;
            tz.zthr = DepthThreshold(vp, V3(inst[k].pos.x, inst[k].pos.z, inst[k].pos.y), DEPTH_MARGIN);
            static bool s_fHid[MAX_SLOTS + 1];          // v17.0 r2: casi entero detrás en el cuadro anterior
            const int id = inst[k].id <= MAX_SLOTS ? inst[k].id : 0;
            tz.zskip = s_fHid[id];
            Copy<true>(tz, S.px, S.w, S.h, X0, Y0);
            s_fHid[id] = MostlyHidden(s_zTotal, s_zHidden, s_fHid[id]);
        } else {
            Copy<true>(t, S.px, S.w, S.h, X0, Y0);
        }
        DrawInfo &d = info[k];
        d.drawn = true;
        d.minX = X0 < 0 ? 0 : X0; d.minY = Y0 < 0 ? 0 : Y0;
        d.maxX = X0 + S.w - 1 > t.w - 1 ? t.w - 1 : X0 + S.w - 1;
        d.maxY = Y0 + S.h - 1 > t.h - 1 ? t.h - 1 : Y0 + S.h - 1;
        d.headX = S.headX + ox; d.headY = S.headY + oy;
        d.depth = sig[k].fw;
    }
    if (s_clock) g_stats.ticksComposite += s_clock() - c0;
}

} // namespace R3D
