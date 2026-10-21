#include "DepthProbe.hpp"
#include "GameHook.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <cstdlib>

using namespace CTRPluginFramework;

namespace DepthProbe {

// nn::gx: añade una orden de 8 palabras a la cola de la GPU (y la dispara si es la primera). Verificado en vivo con un
// punto de vigilancia de GDB sobre la cola del GSP (0x10002800) y descompilado (2026-10-19). En Azahar la cola se
// ejecuta al momento: cuando llega una orden, las anteriores (el dibujo del mundo) ya se hicieron.
static const u32 FN_GX_ADD   = 0x001740C4;
static const u32 kGxProlog[3] = { 0xE92D47F0, 0xE5903000, 0xE3530000 };   // push {r4-r10,lr}; ldr r3,[r0]; cmp r3,#0
static const u32 GX_FILL = 2, GX_TRANSFER = 3;
static const u32 TOP_OUT_DIMS = (400u << 16) | 240u;   // copia a la pantalla de arriba: 240 de ancho x 400 de alto

static bool  s_installed = false, s_ok = false;
static Hook  s_hook;

// Lienzo actual (de la última orden de borrado): color y profundidad, mismas medidas y teselas de 8x8 (Morton)
static volatile u32 s_colorBase = 0, s_depthBase = 0, s_depthEnd = 0;

// Lo que pide el OSD para el SIGUIENTE cuadro
static const int MAX_RECTS = 16;
static Rect s_want[MAX_RECTS];
static volatile int s_nWant = 0;

// Captura: profundidad (24 bits) de las zonas pedidas, en coordenadas de la pantalla de arriba (400x240)
static u32 *s_buf = nullptr;                 // 400*240 palabras, se reserva al instalar
static volatile u32 s_frame = 0;
static Rect s_have[MAX_RECTS];
static volatile int s_nHave = 0;
static volatile u32 s_captures = 0;
static volatile u64 s_ticks = 0;
static volatile bool s_dumpReq = false, s_dumpReady = false;

static inline u32 Morton(u32 x, u32 y) {
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}
// posición (x, y) dentro de su tesela de 8x8 de cada una de sus 64 palabras (inverso de Morton)
static u8 s_demX[64], s_demY[64];

// Píxel de la pantalla de arriba (sx 0..399 izquierda->derecha, sy 0..239 arriba->abajo) -> dirección de su palabra en
// el lienzo (región de la pantalla de arriba que empieza en 'base', 256 de ancho en teselas de 8x8).
// El lienzo está en vertical (240 x 400): fx = 239 - sy, fy = sx (comprobado con mmo_dev_depth/color).
static inline u32 TexelAddr(u32 base, int sx, int sy) {
    u32 fx = 239 - (u32)sy, fy = (u32)sx;
    u32 tile = (fy >> 3) * 32 + (fx >> 3);
    return base + ((tile << 6) + Morton(fx & 7, fy & 7)) * 4;
}

// Copia la profundidad del rectángulo que contiene todo lo pedido, por teselas enteras: cada tesela son 64 palabras
// seguidas (una lectura lineal) que se reparten con la tabla inversa de Morton. El lienzo está en vertical:
// fx = 239 - sy, fy = sx.
static void Capture(u32 depthTop) {
    u64 t0 = svcGetSystemTick();
    int n = s_nWant;
    if (n > MAX_RECTS) n = MAX_RECTS;
    if (n <= 0) return;
    Rect B = s_want[0];
    for (int r = 1; r < n; r++) {
        const Rect &R = s_want[r];
        if (R.x0 < B.x0) B.x0 = R.x0;
        if (R.y0 < B.y0) B.y0 = R.y0;
        if (R.x1 > B.x1) B.x1 = R.x1;
        if (R.y1 > B.y1) B.y1 = R.y1;
    }
    if (B.x0 < 0) B.x0 = 0;
    if (B.y0 < 0) B.y0 = 0;
    if (B.x1 > 400) B.x1 = 400;
    if (B.y1 > 240) B.y1 = 240;
    if (B.x0 >= B.x1 || B.y0 >= B.y1) { s_nHave = 0; return; }
    int tx0 = (239 - (B.y1 - 1)) >> 3, tx1 = (239 - B.y0) >> 3;      // teselas en fx
    int ty0 = B.x0 >> 3, ty1 = (B.x1 - 1) >> 3;                       // teselas en fy
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            const volatile u32 *src = (const volatile u32 *)(depthTop + ((u32)(ty * 32 + tx) << 8));
            const int bx = ty << 3, by = 239 - (tx << 3);               // pantalla: sx = fy, sy = 239 - fx
            for (int i = 0; i < 64; i++)
                s_buf[(by - s_demX[i]) * 400 + bx + s_demY[i]] = src[i] & 0xFFFFFF;
        }
    Rect H;                                                           // lo escrito (teselas enteras) en la pantalla
    H.x0 = (s16)(ty0 << 3); H.x1 = (s16)((ty1 + 1) << 3);
    H.y0 = (s16)(239 - ((tx1 << 3) + 7)); H.y1 = (s16)(240 - (tx0 << 3));
    if (H.x1 > 400) H.x1 = 400;
    if (H.y0 < 0) H.y0 = 0;
    s_have[0] = H;
    s_nHave = 1;
    s_frame = s_frame + 1;
    s_captures = s_captures + 1;
    s_ticks = s_ticks + (svcGetSystemTick() - t0);
}

static void Dump(u32 depthTop, u32 colorTop) {
    // pruebas: TODO el cuadro (profundidad + color) a un búfer temporal, se escribe desde el OSD
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 400; x++)
            s_buf[y * 400 + x] = *(volatile u32 *)TexelAddr(depthTop, x, y) & 0xFFFFFF;
    (void)colorTop;
    s_have[0].x0 = 0; s_have[0].y0 = 0; s_have[0].x1 = 400; s_have[0].y1 = 240;   // At() vale para todo el cuadro
    s_nHave = 1;
    s_dumpReady = true;
}

static void OnGxCommand(const u32 *cmd) {
    u32 id = cmd[0] & 0xFF;
    if (id == GX_FILL) {
        // borrado del lienzo: buf0 = color, buf1 = profundidad (valor 0xFFFFFFFF = lejos del todo + plantilla)
        if (cmd[4] >= 0x1F000000 && cmd[4] < 0x1F600000 && cmd[6] > cmd[4] && cmd[5] == 0xFFFFFFFFu) {
            s_colorBase = cmd[1]; s_depthBase = cmd[4]; s_depthEnd = cmd[6];
        }
        return;
    }
    if (id != GX_TRANSFER || cmd[4] != TOP_OUT_DIMS) return;
    u32 cb = s_colorBase, db = s_depthBase, de = s_depthEnd;
    if (!cb || !db || cmd[1] < cb) return;
    u32 off = cmd[1] - cb;                               // la pantalla de arriba empieza a esta distancia del lienzo
    u32 depthTop = db + off;
    if (depthTop + 256 * 400 * 4 > de) return;           // no cabe: medidas inesperadas, no leer
    if (s_dumpReq && !s_dumpReady) { s_dumpReq = false; Dump(depthTop, cmd[1]); return; }
    if (s_nWant > 0) Capture(depthTop);
}

static u32 GxAddHook(u32 holder, u32 *cmd) {
    if (s_ok && cmd) OnGxCommand(cmd);
    return HookContext::GetCurrent().OriginalFunction<u32>(holder, cmd);
}

bool Install() {
    if (s_installed) return s_ok;
    s_installed = true;
    if (!GameHook::Active()) return false;               // juego distinto: no tocar nada
    for (int i = 0; i < 3; i++)
        if (*(volatile u32 *)(FN_GX_ADD + i * 4) != kGxProlog[i]) { Log::Write("profundidad: funcion distinta, desactivado"); return false; }
    for (u32 i = 0; i < 64; i++) {
        u32 x = 0, y = 0;
        for (u32 b = 0; b < 3; b++) { x |= ((i >> (2 * b)) & 1) << b; y |= ((i >> (2 * b + 1)) & 1) << b; }
        s_demX[i] = (u8)x; s_demY[i] = (u8)y;
    }
    s_buf = (u32 *)malloc(400 * 240 * 4);
    if (!s_buf) { Log::Write("profundidad: sin memoria"); return false; }
    s_hook.InitializeForMitm(FN_GX_ADD, (u32)GxAddHook);
    if (s_hook.Enable() != HookResult::Success) { Log::Write("profundidad: gancho fallo"); return false; }
    s_ok = true;
    Log::Write("profundidad: gancho en la cola de la GPU instalado");
    return true;
}

bool Active() { return s_ok; }

void Want(const Rect *r, int n) {
    if (n > MAX_RECTS) n = MAX_RECTS;
    for (int i = 0; i < n; i++) s_want[i] = r[i];
    s_nWant = n;
}

bool At(int x, int y, u32 &depth) {
    if (!s_ok || x < 0 || y < 0 || x >= 400 || y >= 240) return false;
    int n = s_nHave;
    for (int r = 0; r < n; r++) {
        const Rect &R = s_have[r];
        if (x >= R.x0 && x < R.x1 && y >= R.y0 && y < R.y1) { depth = s_buf[y * 400 + x]; return true; }
    }
    return false;
}

u32 Frame() { return s_frame; }
const u32 *Buffer() { return s_buf; }
bool Region(Rect &r) {
    if (!s_ok || s_nHave <= 0) return false;
    r = s_have[0];
    return true;
}
void Stats(u32 &captures, u64 &ticks) { captures = s_captures; ticks = s_ticks; }

void RequestDump() { if (s_ok) s_dumpReq = true; }

bool ServiceDump() {
    if (!s_dumpReady) return false;
    static int n = 0;
    char path[40];
    snprintf(path, sizeof(path), "mmo_dev_depth_%d.bmp", ++n);
    // escala de grises: el rango real de la escena (sin el fondo 0xFFFFFF)
    u32 lo = 0xFFFFFF, hi = 0;
    for (int i = 0; i < 400 * 240; i++) { u32 d = s_buf[i] & 0xFFFFFF; if (d != 0xFFFFFF) { if (d < lo) lo = d; if (d > hi) hi = d; } }
    if (hi <= lo) hi = lo + 1;
    File f;
    if (File::Open(f, path, File::RWC | File::TRUNCATE) == 0) {
        u8 hdr[54] = { 'B', 'M' };
        u32 rowBytes = 400 * 3, size = 54 + rowBytes * 240;
        memcpy(hdr + 2, &size, 4); u32 v = 54; memcpy(hdr + 10, &v, 4); v = 40; memcpy(hdr + 14, &v, 4);
        s32 w = 400, h = 240; memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
        u16 planes = 1, bpp = 24; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
        f.Write(hdr, 54);
        static u8 row[400 * 3];
        for (int y = 239; y >= 0; y--) {
            for (int x = 0; x < 400; x++) {
                u32 d = s_buf[y * 400 + x] & 0xFFFFFF;
                u8 g = d == 0xFFFFFF ? 0 : (u8)(255 - (u64)(d - lo) * 255 / (hi - lo));   // claro = cerca
                row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = g;
            }
            f.Write(row, sizeof(row));
        }
        f.Close();
    }
    Log::Write("dev: profundidad %s (rango %06lX..%06lX)", path, (unsigned long)lo, (unsigned long)hi);
    s_dumpReady = false;
    return true;
}

} // namespace DepthProbe
