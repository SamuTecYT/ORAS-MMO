#include "Follower3D.hpp"
#include "HeroAssets.hpp"
#include "Lzma.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdlib>
#include <cstring>
#include <cmath>

using namespace CTRPluginFramework;

extern "C" char *fake_heap_start, *fake_heap_end;     // libsysbase: el montón del plugin

namespace Follower3D {

// ---- Paquete (mmo_pokes_3d.bin) -----------------------------------------------------------------------------
// "MMO3D3\0\0" (v16.3, LZMA) o "MMO3D2\0\0" (v16.2, LZ11) | u16 count=722 | u16 refPx | u16 nDirs=5 | u16 nFrames
// (5 o 9) | u32 off[722*5] | u32 len[722*5]. Bloque (especie s, dirección d) en s*5+d:
//   MMO3D3: u32 tamaño descomprimido | u8 propiedades LZMA ((pb*5+lp)*9+lc) | flujo LZMA1 crudo
//   MMO3D2: LZ11 (0x11 | u24 tamaño | datos)
// Descomprimido (igual en los dos):
//   u16 nPal | u16 flags | u16 pal[nPal] | nFrames x {u8 w, u8 h, s16 ax, s16 ay, u16 0, u32 off,
//   u32 len} | filas RLE (por fila u8 nTramos, y por tramo u8 x, u8 n, n x u8 índice; 0 = transparente, i = pal[i-1])
//   flags: bit0 anda con patas, bit1 cuadrúpedo, byte alto = zancada (unidades del mundo por ciclo) con patas, o
//   duración de su bucle en pasos de 20 ms sin patas (0 = no viene: paquete viejo de 5 cuadros)
static const int COUNT = 722, NBLK = COUNT * DIRS;
static const int MAXDIM = 128;                 // el horneado nunca pasa de 128 x 128 (make_3d_pack2.py)
static const u32 MAXRAW = 96 * 1024;           // tope de un bloque descomprimido (el mayor ronda 75 KB)
static File  s_pack;
static bool  s_packTried = false, s_packOk = false, s_lzma = false;
static int   s_refPx = 72;
static int   s_frames = 5;                       // cuadros por dirección del paquete (5 o 9)
static u32  *s_off = nullptr, *s_len = nullptr;   // tablas del paquete (se leen al abrirlo: 29 KB)
static u16   s_probs[Lzma::PROBS_LC_LP_3];        // modelo de LZMA (lc + lp <= 3): 16 KB fijos, sin reservas (v17.0 r2: +1, ver Lzma.hpp)

// ---- Memoria: un ESPACIO PROPIO de tamaño fijo (v16.3) ----------------------------------------------------------
// Antes cada bloque era un malloc suelto (hasta 75 KB seguidos) y se precargaban las 5 direcciones de cada especie:
// con la memoria del plugin fragmentada (amigos, menú) los grandes no cabían, fallaban y salía el ícono viejo
// (registro de una prueba: "no se pudo cargar la especie 297" cientos de veces). Ahora:
//  - un solo bloque reservado al arrancar el plugin (y al cerrar el menú SELECT si se soltó al abrirlo), cuando hay
//    huecos grandes libres: 640 KB con el plugin de 10 MB (caben ~13 Pokémon a la vez), 256 KB con el de 5 MB;
//  - dentro, cada bloque va en el primer hueco libre donde quepa (sin mover nada) y solo si no hay hueco se juntan
//    los bloques (compactar); la lectura comprimida y la descompresión también ocurren dentro (sin reservas aparte);
//  - solo se lee la dirección que se está viendo, y lo que lleva medio segundo sin dibujarse deja su sitio.
static const u32 ARENA_SIZES[] = { 640 * 1024, 512 * 1024, 384 * 1024, 256 * 1024, 192 * 1024, 160 * 1024, 128 * 1024 };
static const u64 IDLE_MS = 500;                  // sin dibujarse en este tiempo = puede dejar su sitio
static const int OUT_MAX = 112;
static u8  *s_idx = nullptr;                   // MAXDIM*MAXDIM índices del cuadro en curso
static u8  *s_arena = nullptr;
static u32  s_cap = 0;                         // los bloques vivos van donde haya hueco (FindGap)
static u64  s_arenaRetry = 0;

// v16.3: dibujos ya reducidos (pasos 1-4 de Draw) de los últimos cuadros usados. Antes se rehacían en cada cuadro del
// juego: ~730.000 ciclos por Pokémon (con 5 en pantalla el juego bajaba a 19 FPS; medido 2026-10-18). Ahora solo se
// rehace cuando cambia la postura, el tamaño (en pasos de 1/128) o la luz; el resto de cuadros solo se copia.
struct OutEntry { u16 sp; u8 dir, f, n; u16 kq, ow, oh; s16 t0, t1, t2; float kk; u16 *buf; u32 cap; u64 use; };
static const int OUTC = 64;                     // con el plugin de 5 MB solo se usan 16 (ver Draw)
static OutEntry s_oc[OUTC];
static u64 s_ocClock = 0;
// 65536 / c redondeado hacia arriba (c = 1..16): r * kRecip[c] >> 16 == r / c para r <= 16 * 255 (sin divisiones:
// el ARM11 no tiene instrucción de dividir y tres divisiones por píxel eran lo más caro)
static const u32 kRecip[17] = { 0, 65536, 32768, 21846, 16384, 13108, 10923, 9363, 8192, 7282, 6554, 5958, 5462, 5042, 4682, 4370, 4096 };

enum { FREE = 0, WANTED, READY, FAILED };
static const int SLOTS = 28;
struct Frame { Dir d; u32 off; };
struct View {
    u16 species; u8 dir, state; u64 lastUse, want, failAt;
    u8 *data; u32 size;                        // data apunta DENTRO del espacio propio
    int npal, flags; float radius;
    Frame fr[MAXFRAMES];
};
static View  s_slot[SLOTS];
static u32   s_used = 0;                       // bytes de bloques vivos
static Mutex s_mutex;                          // casillas y contenido del espacio (el OSD lo tiene entre Begin y End)
static Mutex s_serveMutex;                     // Serve() mientras escribe en el espacio (Release espera a que acabe)

Prof g_prof;
int RefPx() { return s_refPx; }
float RadiusPx(const View *v) { return v ? v->radius : 0.f; }
bool Walks(const View *v) { return v && (v->flags & 1); }
int Frames() { return s_frames; }
float Stride(const View *v) { return v && (v->flags & 1) ? (float)((v->flags >> 8) & 0xFF) : 0.f; }
int CycleMs(const View *v) { return v && !(v->flags & 1) ? ((v->flags >> 8) & 0xFF) * 20 : 0; }

static View *Find(u16 sp, int d) {
    for (auto &s : s_slot) if (s.state != FREE && s.species == sp && s.dir == d) return &s;
    return nullptr;
}

// Suelta la casilla (su sitio en el espacio se recupera al compactar). Con s_mutex tomado.
static void Drop(View &c) {
    if (c.data) s_used -= c.size;
    c.data = nullptr; c.size = 0; c.state = FREE;
}

// Primer hueco libre de al menos 'need' bytes entre los bloques vivos, SIN mover nada. nullptr = no hay. Con s_mutex.
static u8 *FindGap(u32 need) {
    u8 *at = s_arena, *end = s_arena + s_cap;
    for (;;) {
        View *next = nullptr;                          // el bloque vivo más bajo desde 'at'
        for (auto &c : s_slot)
            if (c.state == READY && c.data >= at && (!next || c.data < next->data)) next = &c;
        u8 *lim = next ? next->data : end;
        if ((u32)(lim - at) >= need) return at;
        if (!next) return nullptr;
        at = next->data + next->size;
    }
}

// Último recurso: junta los bloques vivos al principio (mueve datos: el OSD espera unos ms). Con s_mutex tomado: el
// OSD no está dibujando (lo tiene entre Begin y End), así que mover es seguro.
static void Compact() {
    u8 *dst = s_arena;
    for (;;) {
        View *next = nullptr;
        for (auto &c : s_slot)
            if (c.state == READY && c.data >= dst && (!next || c.data < next->data)) next = &c;
        if (!next) break;
        if (next->data != dst) { memmove(dst, next->data, next->size); next->data = dst; }
        dst += next->size;
    }
}

// Sitio para 'need' bytes seguidos: un hueco libre; si no hay, se suelta lo que lleva medio segundo sin dibujarse (el
// más viejo primero) y después las direcciones "de repuesto" de la misma especie; y solo al final se compacta. v16.3:
// antes se compactaba siempre y, con 640 KB, mover medio espacio en cada carga bajaba el juego a 19 FPS. Con s_mutex.
static u8 *MakeRoom(u32 need, const View &self, u16 sp, u64 now) {
    if (need > s_cap) return nullptr;
    for (int pass = 0; pass < 2; pass++)
        for (;;) {
            if (u8 *g = FindGap(need)) return g;
            View *lru = nullptr;
            for (auto &c : s_slot) {
                if (c.state != READY || &c == &self) continue;
                bool idle = c.lastUse + IDLE_MS < now, spare = pass == 1 && c.species == sp;
                if ((idle || spare) && (!lru || c.lastUse < lru->lastUse)) lru = &c;
            }
            if (!lru) break;
            Drop(*lru);
        }
    Compact();
    return FindGap(need);
}

// Pide (sp, d). Reutiliza un hueco libre o el que lleve más tiempo sin dibujarse (nunca uno en uso).
static void Want(u16 sp, int d, u64 now) {
    View *s = Find(sp, d);
    if (s) {
        if (s->state == FAILED && now - s->failAt > 1000) { s->state = WANTED; s->want = now; }
        return;
    }
    View *victim = nullptr;
    for (auto &c : s_slot) {
        if (c.state == WANTED) continue;
        if (c.state == FREE) { victim = &c; break; }
        if (now - c.lastUse < IDLE_MS) continue;
        if (!victim || c.lastUse < victim->lastUse) victim = &c;
    }
    if (!victim) return;
    Drop(*victim);
    victim->species = sp; victim->dir = (u8)d; victim->state = WANTED; victim->want = now; victim->lastUse = now;
}

const View *Begin(u16 species, int dir, u64 now) {
    if (species < 1 || species > 721 || (s_packTried && !s_packOk)) return nullptr;
    if (dir < 0) dir = 0;
    if (dir >= DIRS) dir = DIRS - 1;
    s_mutex.Lock();
    View *s = Find(species, dir);
    if (s && s->state == READY) { s->lastUse = now; return s; }        // queda bloqueado hasta End()
    // no está: se pide y, mientras llega (unas centésimas), la dirección ya cargada más parecida de la especie
    Want(species, dir, now);
    View *best = nullptr;
    for (int k = 1; k < DIRS && !best; k++)
        for (int sg = -1; sg <= 1 && !best; sg += 2) {
            int d2 = dir + sg * k;
            if (d2 < 0 || d2 >= DIRS) continue;
            View *c = Find(species, d2);
            if (c && c->state == READY) best = c;
        }
    if (best) { best->lastUse = now; return best; }
    s_mutex.Unlock();
    return nullptr;
}

void End() { s_mutex.Unlock(); }

void Release() {
    CTRPluginFramework::Lock ls(s_serveMutex);          // espera a que Serve() termine de escribir en el espacio
    CTRPluginFramework::Lock l(s_mutex);
    for (auto &s : s_slot) Drop(s);
    s_used = 0;
    free(s_arena); s_arena = nullptr; s_cap = 0; s_arenaRetry = 0;
    free(s_idx); s_idx = nullptr;
    for (auto &e : s_oc) { free(e.buf); e.buf = nullptr; e.cap = 0; e.sp = 0; }
}

static bool OpenPack() {
    s_packTried = true;
    static const char *const kPaths[] = { "mmo_pokes_3d.bin", "/luma/plugins/mmo_pokes_3d.bin",
                                          "/luma/plugins/000400000011C400/mmo_pokes_3d.bin",
                                          "/luma/plugins/000400000011C500/mmo_pokes_3d.bin" };
    bool opened = false;
    for (const char *path : kPaths) if (!opened && File::Open(s_pack, path, File::READ) == 0) opened = true;
    if (opened) {
        u8 head[16];
        bool v3 = false;
        if (s_pack.Read(head, 16) == 0 && (!memcmp(head, "MMO3D2", 6) || (v3 = !memcmp(head, "MMO3D3", 6)) != 0) &&
            (head[8] | (head[9] << 8)) == COUNT && (head[12] | (head[13] << 8)) == DIRS &&
            (head[14] | (head[15] << 8)) >= 5 && (head[14] | (head[15] << 8)) <= MAXFRAMES) {
            s_frames = head[14] | (head[15] << 8);
            s_lzma = v3;
            s_off = (u32 *)malloc(NBLK * 4); s_len = (u32 *)malloc(NBLK * 4);
            if (s_off && s_len && s_pack.Read(s_off, NBLK * 4) == 0 && s_pack.Read(s_len, NBLK * 4) == 0) {
                int ref = head[10] | (head[11] << 8);
                if (ref >= 16 && ref <= 255) { s_refPx = ref; s_packOk = true; }
            }
        }
    }
    Log::Write("acompañantes 3D: paquete %s (ref %d px, %d cuadros, %s)", s_packOk ? "listo" : "NO ENCONTRADO: sin acompañantes",
               s_refPx, s_frames, s_lzma ? "LZMA" : "LZ11");
    return s_packOk;
}

// Comprueba el bloque descomprimido (tamaños, desplazamientos y filas RLE dentro de los límites) y lo resume
static bool Parse(View &v, const u8 *b, u32 n) {
    if (n < 4) return false;
    int npal = b[0] | (b[1] << 8);
    if (npal < 1 || npal > 255 || 4 + 2u * npal + 16u * s_frames > n) return false;
    v.npal = npal; v.flags = b[2] | (b[3] << 8);
    float rad = 0;
    for (int f = 0; f < s_frames; f++) {
        const u8 *e = b + 4 + 2 * npal + 16 * f;
        Frame &F = v.fr[f];
        F.d.w = e[0]; F.d.h = e[1];
        F.d.ax = (s16)(e[2] | (e[3] << 8)); F.d.ay = (s16)(e[4] | (e[5] << 8));
        u32 off = e[8] | (e[9] << 8) | (e[10] << 16) | ((u32)e[11] << 24);
        u32 len = e[12] | (e[13] << 8) | (e[14] << 16) | ((u32)e[15] << 24);
        if (!F.d.w || !F.d.h || F.d.w > MAXDIM || F.d.h > MAXDIM || off > n || len > n - off) return false;
        u32 p = off, end = off + len;
        for (int y = 0; y < F.d.h; y++) {
            if (p >= end) return false;
            int runs = b[p++];
            for (int r = 0; r < runs; r++) {
                if (p + 2 > end) return false;
                int x = b[p], cnt = b[p + 1];
                p += 2;
                if (x + cnt > F.d.w || p + cnt > end) return false;
                p += cnt;
            }
        }
        F.off = off;
        if (F.d.w * 0.5f > rad) rad = F.d.w * 0.5f;
    }
    v.radius = rad;
    return true;
}

// Reserva el espacio propio (el mayor tamaño que quepa). false = no hay memoria ahora: se reintenta en 2 s.
static bool EnsureArena(u64 now) {
    if (s_arena) return true;
    if (now < s_arenaRetry) return false;
    // con el montón del plugin de 5 MB (unos 3,3 MB) no se pasa de 256 KB: el resto lo necesitan los modelos y el menú
    const u32 heap = (fake_heap_start && fake_heap_end > fake_heap_start) ? (u32)(fake_heap_end - fake_heap_start) : 0;
    const u32 top = heap > 6u * 1024 * 1024 ? 640u * 1024 : 256u * 1024;
    for (u32 sz : ARENA_SIZES) {
        if (sz > top) continue;
        s_arena = (u8 *)malloc(sz);
        if (s_arena) { s_cap = sz; Log::Write("acompañantes 3D: espacio propio de %lu KB", (unsigned long)(sz / 1024)); return true; }
    }
    s_arenaRetry = now + 2000;
    Log::Write("acompañantes 3D: sin memoria para su espacio (se reintenta)");
    return false;
}

void Reserve() {
    CTRPluginFramework::Lock l(s_mutex);
    EnsureArena(0);
}

bool Serve() {
    for (int guard = 0; guard < 12; guard++) {
        int pick = -1; u16 sp = 0; int d = 0;
        {
            CTRPluginFramework::Lock l(s_mutex);
            u64 best = ~0ull;
            for (int i = 0; i < SLOTS; i++)
                if (s_slot[i].state == WANTED && s_slot[i].want < best) { best = s_slot[i].want; pick = i; }
            if (pick >= 0) { sp = s_slot[pick].species; d = s_slot[pick].dir; }
        }
        if (pick < 0) return false;
        if (!s_packTried) OpenPack();
        CTRPluginFramework::Lock serving(s_serveMutex);
        u64 now = osGetTime();
        u32 bi = (u32)sp * DIRS + (u32)d;
        u32 clen = s_packOk ? s_len[bi] : 0, coff = s_packOk ? s_off[bi] : 0;
        // cabecera del bloque: tamaño descomprimido
        u8 hd[8];
        u32 raw = 0;
        bool ok = clen >= 10 && clen <= MAXRAW && s_pack.Seek((s64)coff, File::SET) == 0 && s_pack.Read(hd, 8) == 0;
        if (ok) raw = s_lzma ? (u32)(hd[0] | (hd[1] << 8) | (hd[2] << 16) | ((u32)hd[3] << 24)) : (u32)(hd[1] | (hd[2] << 8) | (hd[3] << 16));
        ok = ok && raw >= 4 && raw <= MAXRAW && (s_lzma || hd[0] == 0x11);
        const u32 size = (raw + 3) & ~3u;
        u8 *dst = nullptr, *src = nullptr;
        bool noRoom = false;
        {
            CTRPluginFramework::Lock l(s_mutex);
            View &s = s_slot[pick];
            if (s.state != WANTED || s.species != sp || s.dir != d) continue;   // la cambiaron mientras tanto
            if (ok && EnsureArena(now)) {
                if ((dst = MakeRoom(size + clen, s, sp, now)) != nullptr) src = dst + size;
                else noRoom = true;
            }
        }
        // fuera del cerrojo (el OSD sigue dibujando): leer el bloque comprimido al final del hueco y descomprimirlo al
        // principio. Nada más escribe ahí (solo este hilo reserva y compacta) y Release espera a s_serveMutex.
        if (dst) {
            ok = s_pack.Seek((s64)coff, File::SET) == 0 && s_pack.Read(src, clen) == 0;
            if (ok && s_lzma) {
                int props = src[4], lc = props % 9, lp = (props / 9) % 5, pb = props / 45;
                ok = props < 9 * 5 * 5 && lc + lp <= 3 && Lzma::ProbCount(lc, lp) <= sizeof(s_probs) / sizeof(s_probs[0]) &&
                     Lzma::Decode(src + 5, clen - 5, dst, raw, lc, lp, pb, s_probs) == (s32)raw;
            } else if (ok) ok = Hero::Lz11Decode(src, clen, dst, raw) == raw;
        } else ok = false;
        View probe;
        memset(&probe, 0, sizeof(probe));
        if (ok) ok = Parse(probe, dst, raw);
        {
            CTRPluginFramework::Lock l(s_mutex);
            View &s = s_slot[pick];
            if (s.state != WANTED || s.species != sp || s.dir != d) continue;   // la cambiaron: su sitio queda libre
            if (ok) {
                u64 use = s.lastUse;
                s = probe;
                s.species = sp; s.dir = (u8)d; s.lastUse = use; s.data = dst; s.size = size; s.state = READY;
                s_used += size;
            } else {
                s.state = FAILED; s.failAt = now;
                static u64 nextLog = 0;
                if (s_packOk && now >= nextLog) {           // como mucho una línea cada 5 s (antes llenaba el registro)
                    nextLog = now + 5000;
                    Log::Write("acompañantes 3D: especie %d (dirección %d) %s (en uso %lu de %lu KB)", sp, d,
                               noRoom ? "no cabe ahora" : !s_arena ? "sin espacio propio" : "dañada en el paquete",
                               (unsigned long)(s_used / 1024), (unsigned long)(s_cap / 1024));
                }
            }
        }
    }
    return true;
}

// ---- Dibujo -------------------------------------------------------------------------------------------------
static inline u16 Pack565(int r, int g, int b) {
    u16 c = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return c ? c : 0x0020;                     // 0 es transparente
}

static int s_lastRect[4];
void LastRect(int &x0, int &y0, int &x1, int &y1) { x0 = s_lastRect[0]; y0 = s_lastRect[1]; x1 = s_lastRect[2]; y1 = s_lastRect[3]; }

bool Draw(const R3D::Target &t, const View *v, int f, bool mirror, float k, float fx, float fy, float sqx, float sqy,
          const int tint[3], const int *holes, int nh, bool cheap) {
    if (!v || !v->data || !(k > 0.05f) || k > 6.f) return false;
    if (f < 0 || f >= s_frames) f = 0;
    const Dir &D = v->fr[f].d;
    const int W = D.w, H = D.h;
    u64 tk0 = svcGetSystemTick();
    // tamaño en pasos del ~2 % (logarítmicos): al acercarse o alejarse de la cámara el tamaño cambia un poco en cada
    // cuadro y obligaba a rehacer el dibujo; la diferencia se ajusta al copiar (estirar < 1 %: invisible)
    int kq = (int)floorf(logf(k) * 48.f + 1000.5f);
    float kk = expf((kq - 1000) / 48.f);
    int ow = (int)(W * kk + 0.5f), oh = (int)(H * kk + 0.5f);
    if (ow > OUT_MAX || oh > OUT_MAX) {          // muy cerca: se calcula al máximo y se amplía al dibujar
        float fs = (float)OUT_MAX / (float)(W > H ? W : H);
        kk = fs; ow = (int)(W * kk); oh = (int)(H * kk);
        if (ow > OUT_MAX) ow = OUT_MAX;
        if (oh > OUT_MAX) oh = OUT_MAX;
    }
    if (ow < 2 || oh < 2) return false;
    // muestras por píxel: n x n (bordes limpios). v16.3: 2 x 2 hasta k = 0,4 (antes 3 x 3 entre 0,45 y 0,75: 9 muestras
    // por píxel, lo más caro de todo el plugin con varios Pokémon; en un dibujo de ~50 px la diferencia no se ve)
    (void)cheap;
    const int n = kk >= 0.4f ? 2 : 3;
    OutEntry *E = nullptr, *lru = nullptr;
    static int ocN = 0;                         // 64 dibujos guardados con memoria de sobra (5 Pokémon andando = ~45)
    if (!ocN) ocN = (fake_heap_start && fake_heap_end - fake_heap_start > 6 * 1024 * 1024) ? OUTC : 16;
    for (int i = 0; i < ocN; i++) {
        OutEntry &e = s_oc[i];
        if (e.buf && e.sp == v->species && e.dir == v->dir && e.f == f && e.kq == kq && e.n == n && e.t0 == tint[0] &&
            e.t1 == tint[1] && e.t2 == tint[2]) { E = &e; break; }
        if (!lru || e.use < lru->use) lru = &e;
    }
    const bool miss = E == nullptr;
    u64 tk1 = tk0, tk2 = tk0, tk3 = tk0;
    if (miss) {
        E = lru;
        E->sp = 0;                                // a medias: no vale hasta terminar
        if (E->cap < (u32)(ow * oh)) {
            free(E->buf);
            E->buf = (u16 *)malloc((size_t)ow * oh * 2);
            E->cap = E->buf ? (u32)(ow * oh) : 0;
        }
        if (!s_idx) s_idx = (u8 *)malloc(MAXDIM * MAXDIM);
        if (!E->buf || !s_idx) return false;
        const u8 *blob = v->data;
        // 1) índices del cuadro
        u8 *idx = s_idx;
        memset(idx, 0, (size_t)W * H);
        u32 p = v->fr[f].off;
        for (int y = 0; y < H; y++) {
            int runs = blob[p++];
            for (int r = 0; r < runs; r++) {
                int x = blob[p], cnt = blob[p + 1];
                p += 2;
                memcpy(idx + y * W + x, blob + p, cnt);
                p += cnt;
            }
        }
        tk1 = svcGetSystemTick();
        // 2) paleta con la luz del ambiente
        int pr[256], pg[256], pb[256];
        const u8 *pal = blob + 4;
        for (int i = 0; i < v->npal; i++) {
            u16 c = (u16)(pal[2 * i] | (pal[2 * i + 1] << 8));
            int r = ((c >> 11) * 527 + 23) >> 6, g = (((c >> 5) & 63) * 259 + 33) >> 6, b = ((c & 31) * 527 + 23) >> 6;
            r = (r * tint[0]) >> 8; g = (g * tint[1]) >> 8; b = (b * tint[2]) >> 8;
            pr[i + 1] = r > 255 ? 255 : r; pg[i + 1] = g > 255 ? 255 : g; pb[i + 1] = b > 255 ? 255 : b;
        }
        // índices sin color en la paleta (un paquete mal hecho o manipulado): negro, nunca restos de memoria
        for (int i = v->npal + 1; i < 256; i++) pr[i] = pg[i] = pb[i] = 0;
        tk2 = svcGetSystemTick();
        // 3) reducción con supermuestreo n x n: cobertura >= 50 % = opaco, color = media de las muestras opacas
        const float inv = 1.f / kk;
        static s16 xs[OUT_MAX * 4], ys[OUT_MAX * 4];
        for (int o = 0; o < ow; o++)
            for (int s2 = 0; s2 < n; s2++) {
                int ix = (int)((o + (s2 + 0.5f) / n) * inv);
                xs[o * n + s2] = (s16)(ix < 0 || ix >= W ? -1 : ix);
            }
        for (int o = 0; o < oh; o++)
            for (int s2 = 0; s2 < n; s2++) {
                int iy = (int)((o + (s2 + 0.5f) / n) * inv);
                ys[o * n + s2] = (s16)(iy < 0 || iy >= H ? -1 : iy);
            }
        u16 *out = E->buf;
        const int need = (n * n + 1) / 2;
        if (n == 2) {
            // 2 x 2: colores empaquetados (r | g << 11 | b << 22: 4 x 255 cabe en 10 bits) y una suma por muestra
            static u32 pk[256];
            pk[0] = 0;
            for (int i = 1; i < 256; i++) pk[i] = (u32)pr[i] | ((u32)pg[i] << 11) | ((u32)pb[i] << 22);
            for (int oy = 0; oy < oh; oy++) {
                u16 *row = out + oy * ow;
                int iy0 = ys[oy * 2], iy1 = ys[oy * 2 + 1];
                const u8 *s0 = iy0 >= 0 ? idx + iy0 * W : nullptr, *s1 = iy1 >= 0 ? idx + iy1 * W : nullptr;
                for (int ox = 0; ox < ow; ox++) {
                    int ix0 = xs[ox * 2], ix1 = xs[ox * 2 + 1];
                    int c0 = (s0 && ix0 >= 0) ? s0[ix0] : 0, c1 = (s0 && ix1 >= 0) ? s0[ix1] : 0;
                    int c2 = (s1 && ix0 >= 0) ? s1[ix0] : 0, c3 = (s1 && ix1 >= 0) ? s1[ix1] : 0;
                    int cnt = (c0 != 0) + (c1 != 0) + (c2 != 0) + (c3 != 0);
                    if (cnt < 2) { row[ox] = 0; continue; }
                    u32 acc = pk[c0] + pk[c1] + pk[c2] + pk[c3];
                    u32 m = kRecip[cnt];
                    row[ox] = Pack565((int)(((acc & 0x7FF) * m) >> 16), (int)((((acc >> 11) & 0x7FF) * m) >> 16), (int)(((acc >> 22) * m) >> 16));
                }
            }
        } else
        for (int oy = 0; oy < oh; oy++) {
            u16 *row = out + oy * ow;
            for (int ox = 0; ox < ow; ox++) {
                int cnt = 0, r = 0, g = 0, b = 0;
                for (int sy = 0; sy < n; sy++) {
                    int iy = ys[oy * n + sy];
                    if (iy < 0) continue;
                    const u8 *src = idx + iy * W;
                    for (int sx = 0; sx < n; sx++) {
                        int ix = xs[ox * n + sx];
                        if (ix < 0) continue;
                        int c = src[ix];
                        if (!c) continue;
                        cnt++; r += pr[c]; g += pg[c]; b += pb[c];
                    }
                }
                if (cnt >= need) {
                    u32 m = kRecip[cnt];
                    row[ox] = Pack565((int)(((u32)r * m) >> 16), (int)(((u32)g * m) >> 16), (int)(((u32)b * m) >> 16));
                } else row[ox] = 0;
            }
        }
        tk3 = svcGetSystemTick();
        // 4) contorno: 1 px de borde más oscuro (como la línea de los modelos del juego). Los oscurecidos nunca son 0,
        //    así que siguen contando como "opacos" para sus vecinos.
        for (int oy = 0; oy < oh; oy++)
            for (int ox = 0; ox < ow; ox++) {
                u16 c = out[oy * ow + ox];
                if (!c) continue;
                bool edge = ox == 0 || oy == 0 || ox == ow - 1 || oy == oh - 1 || !out[oy * ow + ox - 1] || !out[oy * ow + ox + 1] ||
                            !out[(oy - 1) * ow + ox] || !out[(oy + 1) * ow + ox];
                if (!edge) continue;
                int r = ((c >> 11) * 9) >> 4, g = (((c >> 5) & 63) * 9) >> 4, b = ((c & 31) * 9) >> 4;   // x0,56
                u16 e = (u16)((r << 11) | (g << 5) | b);
                out[oy * ow + ox] = e ? e : 0x0020;
            }
        E->sp = v->species; E->dir = v->dir; E->f = (u8)f; E->n = (u8)n; E->kq = (u16)kq; E->ow = (u16)ow; E->oh = (u16)oh;
        E->t0 = (s16)tint[0]; E->t1 = (s16)tint[1]; E->t2 = (s16)tint[2]; E->kk = kk;
    }
    E->use = ++s_ocClock;
    u64 tk4 = svcGetSystemTick();
    // 5) a la pantalla: pies en (fx, fy), aplastamiento y espejo
    ow = E->ow; oh = E->oh; kk = E->kk;
    float sc = k / kk;
    int dw = (int)(ow * sc * sqx + 0.5f), dh = (int)(oh * sc * sqy + 0.5f);
    if (dw < 1 || dh < 1) return false;
    float ax = (mirror ? (W - D.ax) : D.ax) * k * sqx, ay = D.ay * k * sqy;
    int x0 = (int)(fx - ax + 0.5f), y0 = (int)(fy - ay + 0.5f);
    s_lastRect[0] = x0; s_lastRect[1] = y0; s_lastRect[2] = x0 + dw; s_lastRect[3] = y0 + dh;
    R3D::BlitScaledKey(t, E->buf, ow, oh, x0, y0, dw, dh, mirror, holes, nh);
    u64 tk5 = svcGetSystemTick();
    g_prof.draws++; g_prof.misses += miss ? 1 : 0; g_prof.idx += tk1 - tk0; g_prof.pal += tk2 - tk1; g_prof.samp += tk3 - tk2;
    g_prof.edge += tk4 - tk3; g_prof.blit += tk5 - tk4; g_prof.px += (u32)(dw * dh);
    return true;
}

// v16.3: el cuadro quieto encajado en una caja (combate de un amigo, en el recuadro de arriba): de 3/4, pies abajo
bool DrawFit(const R3D::Target &t, u16 species, int x, int y, int w, int h, bool faceRight, u64 now) {
    const View *v = Begin(species, 1, now);
    if (!v) return false;
    const Dir &D = v->fr[0].d;
    float k = (float)h / D.h, kw = (float)w / D.w;
    if (kw < k) k = kw;
    if (k > 1.f) k = 1.f;
    float left = x + (w - D.w * k) * 0.5f, top = y + h - D.h * k;
    bool mirror = !faceRight;
    float fx = left + (mirror ? (D.w - D.ax) : D.ax) * k, fy = top + D.ay * k;
    static const int kWhite[3] = { 256, 256, 256 };
    bool ok = Draw(t, v, 0, mirror, k, fx, fy, 1.f, 1.f, kWhite, nullptr, 0);
    End();
    return ok;
}

} // namespace Follower3D
