#include "GameMaps.hpp"
#include "MapLowerPanes.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>

using namespace CTRPluginFramework;

namespace GameMaps {

static const u32 GS_STATIC      = 0x0062F7C4;
static const u32 TM_STUB_LIT    = 0x0019C124;     // importación de code.bin -> DllTownmap 0x3CC09654
static const u32 RO_UNRESOLVED  = 0x0050DDA8;
static const u32 TM_STUB_TARGET = 0x9654;         // desplazamiento del destino dentro del CRO
static const u32 TM_VT_PROC     = 0xF488;         // app::townmap::Proc
static const u32 TM_VT_MAPVIEW  = 0xF4B4;         // app::townmap::MapView
static const int FIRST_PANE = 2, LAST_PANE = 0x93; // los puntos del mapa (MapView los recorre al crearse)
// AreaNav (DllField, KB §13.5): FM+0x160 -> +0 gestor de procesos del menú táctil -> +0x10 -> +0xC FieldMenu,
// FieldMenu+0x54 FM_Pmn, FM_Pmn+0x138 PMN_MapNavi (solo existe con la página del mapa), +0x10 -> +0xA8 layout map_lower
static const u32 DF_LIT = 0x0019C354, DF_OFF = 0x0008EE38;
static const u32 DF_VT_FIELDMENU = 0x0010B07C, DF_VT_FMPMN = 0x0010A980, DF_VT_MAPNAVI = 0x0010A8B4;
static const int NAV_LAST_PANE = 0x1A0;            // el AreaNav coloca más puntos (iconos de lugares)

static inline bool Readable(u32 a, u32 len) {
    if (!a || (a & 3)) return false;
    return Process::CheckAddress(a & ~0xFFFu, MEMPERM_READ) && Process::CheckAddress((a + len - 1) & ~0xFFFu, MEMPERM_READ);
}
static inline bool Rd(u32 a, u32 &v) { if (!Readable(a, 4)) return false; v = *(volatile u32 *)a; return true; }
static inline bool RdF(u32 a, float &f) { u32 v; if (!Rd(a, v)) return false; memcpy(&f, &v, 4); return std::isfinite(f); }
static inline bool Heap(u32 a) { return a >= 0x08000000 && a < 0x10000000 && !(a & 3); }
// Los layouts (panes y su tabla de nombres) viven en la memoria LINEAL (comprobado en vivo: 0x15xxxxxx).
// Se leen pocas palabras y solo al cambiar el mapa (§13.6: leer memoria lineal puede costar si hay GPU cerca).
static inline bool HeapOrLinear(u32 a) {
    return !(a & 3) && ((a >= 0x08000000 && a < 0x10000000) || (a >= 0x14000000 && a < 0x1C000000) || (a >= 0x30000000 && a < 0x38000000));
}

// ¿Qué MapView está abierto? 0 si el mapa de Vuelo no está en pantalla
static u32 FindMapView(u32 &layoutInst) {
    u32 lit;
    if (!Rd(TM_STUB_LIT, lit) || lit == RO_UNRESOLVED || lit < 0x00100000 || lit >= 0x04000000) return 0;
    u32 base = lit - TM_STUB_TARGET;
    if (base & 0xFFF) return 0;
    u32 gs, gpm, slot, proc, vt, ms, gw, mv;
    if (!Rd(GS_STATIC, gs) || !Heap(gs) || !Rd(gs + 0x14, gpm) || !Heap(gpm) || !Rd(gpm + 0x10, slot) || !Heap(slot) ||
        !Rd(slot + 0xC, proc) || !Heap(proc) || !Rd(proc, vt) || vt != base + TM_VT_PROC) return 0;
    if (!Rd(proc + 0x24, ms) || !Heap(ms) || !Rd(ms + 0x18, gw) || !Heap(gw) || !Rd(gw + 0xA4, mv) || !Heap(mv) ||
        !Rd(mv, vt) || vt != base + TM_VT_MAPVIEW) return 0;
    u32 lw, tbl, lay, li;
    if (!Rd(mv + 0x5C, lw) || !HeapOrLinear(lw) || !Rd(lw + 0x1C, tbl) || !HeapOrLinear(tbl) || !Rd(tbl, lay) ||
        !HeapOrLinear(lay) || !Rd(lay + 4, li) || !HeapOrLinear(li)) return 0;
    layoutInst = li;
    return mv;
}

static const MapLower::PaneRef *FindRef(const char *name) {
    for (int i = 0; i < MapLower::PANE_COUNT; i++)
        if (!strcmp(MapLower::PANES[i].name, name)) return &MapLower::PANES[i];
    return nullptr;
}

// Puntos del mapa que el juego tiene colocados: pane + su posición en la imagen. Se arma una vez por MapView
// (emparejar nombres cuesta); después cada ajuste solo lee 2 números por punto.
struct Pt { u32 pane; float ix, iy; };
static Pt  s_pts[NAV_LAST_PANE + 1];
static int s_npts = 0;

static bool BuildPoints(u32 li, int lastPane) {
    s_npts = 0;
    u32 cache, nt0, nt, count;
    if (!Rd(li + 0xC8, cache) || !HeapOrLinear(cache) || !Rd(li + 0xBC, nt0) || !HeapOrLinear(nt0) || !Rd(nt0 + 8, nt) || !HeapOrLinear(nt) ||
        !Rd(nt, count) || count < (u32)FIRST_PANE || count > 2000) return false;
    int last = (int)count - 1 < lastPane ? (int)count - 1 : lastPane;
    for (int idx = FIRST_PANE; idx <= last; idx++) {
        u32 p;
        if (!Rd(cache + idx * 4, p) || !HeapOrLinear(p) || !Readable(p + 0x80, 0x30)) continue;
        u32 na = nt + 4 + idx * 32;
        if (!Readable(na, 16)) continue;
        char name[17];
        for (int k = 0; k < 16; k++) name[k] = *(volatile char *)(na + k);
        name[16] = 0;
        const MapLower::PaneRef *r = FindRef(name);
        if (!r) continue;
        Pt &q = s_pts[s_npts++];
        q.pane = p; q.ix = r->cx2 * 0.5f; q.iy = r->cy2 * 0.5f;
    }
    return s_npts >= 6;
}

// Ajuste robusto (escala uniforme + desplazamiento) con las posiciones REALES de esos puntos ahora mismo.
// Comprobado en vivo (AreaNav 2026-10-16): los panes ocultos tienen la matriz a (0,0) y se descartan; con el
// resto se prueban pares de puntos (tipo RANSAC) y gana la transformación con más puntos a < 2 px.
static bool Fit(Xform &xf) {
    static float ix[NAV_LAST_PANE + 1], iy[NAV_LAST_PANE + 1], sx[NAV_LAST_PANE + 1], sy[NAV_LAST_PANE + 1];
    int n = 0;
    for (int i = 0; i < s_npts; i++) {
        float tx, ty;
        if (!RdF(s_pts[i].pane + 0x8C, tx) || !RdF(s_pts[i].pane + 0x9C, ty) || fabsf(tx) > 4000.f || fabsf(ty) > 4000.f) continue;
        if (tx == 0.f && ty == 0.f) continue;                 // pane oculto: el juego no le calcula la posición
        ix[n] = s_pts[i].ix; iy[n] = s_pts[i].iy;
        sx[n] = 160.f + tx; sy[n] = 120.f - ty;
        n++;
    }
    xf.pairs = n;
    if (n < 6) return false;
    int bestIn = 0; float bs = 0, bx = 0, by = 0;
    for (int k = 0; k < 64; k++) {
        int i = (k * 37 + 1) % n, j = (k * 53 + 7 + n / 2) % n;
        if (i == j) continue;
        float di = hypotf(ix[i] - ix[j], iy[i] - iy[j]);
        if (di < 25.f) continue;
        float s = hypotf(sx[i] - sx[j], sy[i] - sy[j]) / di;
        if (!(s > 0.2f && s < 6.f)) continue;
        float ox = sx[i] - s * ix[i], oy = sy[i] - s * iy[i];
        int in = 0;
        for (int q = 0; q < n; q++) if (hypotf(ox + s * ix[q] - sx[q], oy + s * iy[q] - sy[q]) < 2.f) in++;
        if (in > bestIn) { bestIn = in; bs = s; bx = ox; by = oy; }
    }
    if (bestIn < 6 || bestIn * 2 < n) { xf.err = 99.f; return false; }
    // afinar el desplazamiento con la media de los que encajan
    float ex = 0, ey = 0, err = 0; int c = 0;
    for (int q = 0; q < n; q++) {
        float dx = sx[q] - (bx + bs * ix[q]), dy = sy[q] - (by + bs * iy[q]);
        if (hypotf(dx, dy) < 2.f) { ex += dx; ey += dy; c++; }
    }
    bx += ex / c; by += ey / c;
    for (int q = 0; q < n; q++) { float e = hypotf(bx + bs * ix[q] - sx[q], by + bs * iy[q] - sy[q]); if (e < 2.f) err += e; }
    xf.err = err / c; xf.pairs = c;
    xf.s = bs; xf.ox = bx; xf.oy = by;
    return true;
}

// v16.2: el menú X (y lo que se abre desde él) se dibuja ENCIMA del AreaNav sin quitarlo: el mapa sigue vivo debajo
// (layout idéntico, comprobado en vivo 2026-10-17 comparando memoria con el menú cerrado y abierto). Lo que cambia es
// el FieldMenu: +0x78 = objeto del menú (0 si cerrado) y el byte +0x9C = 1 mientras está abierto. Con cualquiera de
// los dos, el mapa está tapado y no se dibujan pines ni "Tu".
static bool s_navCovered = false;
static bool MenuCovers(u32 proc) {
    u32 obj = 0, w = 0;
    Rd(proc + 0x78, obj);
    Rd(proc + 0x9C, w);
    bool covered = obj != 0 || (w & 0xFF) != 0;
    if (covered != s_navCovered) {
        s_navCovered = covered;
        Log::Write("areanav: %s (menu %08lX, marca %02lX)", covered ? "tapado por un menu" : "visible otra vez",
                   (unsigned long)obj, (unsigned long)(w & 0xFF));
    }
    return covered;
}

// AreaNav abierto ahora: devuelve la instancia del layout map_lower (0 si no)
static u32 FindAreaNav(u32 &owner, bool &covered) {
    u32 lit;
    if (!Rd(DF_LIT, lit) || lit == RO_UNRESOLVED || lit <= DF_OFF) return 0;
    u32 base = lit - DF_OFF;
    if (base & 0xFFF) return 0;
    u32 gs, fm, holder, mgr, slot, proc, vt, pmn, nav, L, lay, li;
    if (!Rd(GS_STATIC, gs) || !Heap(gs) || !Rd(gs + 0x48, fm) || !Heap(fm)) return 0;
    if (!Rd(fm + 0x160, holder) || !Heap(holder) || !Rd(holder, mgr) || !Heap(mgr) || !Rd(mgr + 0x10, slot) || !Heap(slot) ||
        !Rd(slot + 0xC, proc) || !Heap(proc) || !Rd(proc, vt) || vt != base + DF_VT_FIELDMENU) return 0;
    if (!Rd(proc + 0x54, pmn) || !Heap(pmn) || !Rd(pmn, vt) || vt != base + DF_VT_FMPMN) return 0;
    if (!Rd(pmn + 0x138, nav) || !Heap(nav) || !Rd(nav, vt) || vt != base + DF_VT_MAPNAVI) return 0;
    if (!Rd(nav + 0x10, L) || !Heap(L) || !Rd(L + 0x98 + 2 * 8, lay) || !HeapOrLinear(lay) || !Rd(lay + 4, li) || !HeapOrLinear(li)) return 0;
    owner = nav;
    covered = MenuCovers(proc);
    return li;
}

// Seguimiento de un mapa (Vuelo o AreaNav): arma los puntos una vez y reajusta solo si se mueven
struct Tracker { u32 owner; float probe[2]; Xform cached; u64 nextFit, nextLog; bool lastOk; const char *name; };

static bool Track(Tracker &T, u32 owner, u32 li, int lastPane, Xform &xf, u64 now) {
    bool fresh = owner != T.owner;
    if (fresh || (s_npts < 6 && now >= T.nextFit)) BuildPoints(li, lastPane);
    // ¿se movió el mapa? se mira la posición de 1 punto (barato); si cambia, se reajusta
    float probe[2] = {0, 0};
    if (s_npts) { RdF(s_pts[0].pane + 0x8C, probe[0]); RdF(s_pts[0].pane + 0x9C, probe[1]); }
    bool changed = fresh || probe[0] != T.probe[0] || probe[1] != T.probe[1];
    if (changed || !T.cached.ok || now >= T.nextFit) {
        Xform f = {};
        f.ok = s_npts >= 6 && Fit(f);
        T.cached = f;
        T.owner = owner; T.probe[0] = probe[0]; T.probe[1] = probe[1];
        T.nextFit = now + (f.ok ? 1000 : 250);
        if (f.ok != T.lastOk || now >= T.nextLog) {
            T.nextLog = now + 10000;
            T.lastOk = f.ok;
            Log::Write("%s: abierto ok=%d puntos=%d/%d escala=%.3f desp=(%.1f,%.1f) err=%.2f",
                       T.name, f.ok, f.pairs, s_npts, f.s, f.ox, f.oy, f.err);
        }
    }
    xf = T.cached;
    return true;
}

static Tracker s_fly = { 0, {0, 0}, {}, 0, 0, false, "vuelo" };
static Tracker s_nav = { 0, {0, 0}, {}, 0, 0, false, "areanav" };

static void HideHeroIcon(u32 owner, u32 li, u64 now);

bool FlyMap(Xform &xf, u64 now) {
    u32 li = 0;
    u32 mv = FindMapView(li);
    if (!mv) { if (s_fly.owner) { s_fly.owner = 0; s_npts = 0; } s_fly.cached.ok = false; xf = s_fly.cached; return false; }
    if (s_nav.owner) { s_nav.owner = 0; s_npts = 0; }
    // v16.1: el mapa grande (el que se abre a pantalla completa) y el de Vuelo son la app DllTownmap con el MISMO
    // layout map_lower: también ahí la cabecita es el pane "b" con la cara "e" (comprobado en vivo)
    HideHeroIcon(mv, li, now);
    return Track(s_fly, mv, li, LAST_PANE, xf, now);
}

// v16.1 (decisión del proyecto): el icono de cabeza del propio juego marca solo la ruta o ciudad (en rutas largas
// queda lejos de donde estás). Se oculta y el plugin dibuja "Tu" en tu punto exacto. Comprobado en vivo
// (2026-10-16): en map_lower es el pane "b" (20x20, padre de la cara "e"), colocado en tu zona; Pane+0xB7 = banderas,
// bit 0 = visible (los panes ocultos lo tienen a 0). Solo se toca ese bit, y solo si el pane es el esperado.
static u32 s_heroOwner = 0, s_heroLi = 0, s_heroPane = 0, s_heroSlot = 0;
static u64 s_heroRetry = 0;
static bool s_heroLogged = false;
static void HideHeroIcon(u32 owner, u32 li, u64 now) {
    if (owner != s_heroOwner || li != s_heroLi) {
        s_heroOwner = owner; s_heroLi = li; s_heroPane = 0; s_heroSlot = 0; s_heroRetry = 0; s_heroLogged = false;
    }
    // recién abierto el mapa puede no estar completo: se busca cada 0,5 s hasta encontrarlo
    if (!s_heroPane && now >= s_heroRetry) {
        s_heroRetry = now + 500;
        const char *why = "sin tabla de nombres";
        u32 cache, nt0, nt, count;
        if (!Rd(li + 0xC8, cache) || !HeapOrLinear(cache) || !Rd(li + 0xBC, nt0) || !HeapOrLinear(nt0) || !Rd(nt0 + 8, nt) ||
            !HeapOrLinear(nt) || !Rd(nt, count) || count > 2000) return;
        why = "no hay pane b";
        for (u32 idx = 0; idx < count && idx < 64; idx++) {
            u32 na = nt + 4 + idx * 32;
            if (!Readable(na, 4)) break;
            if (*(volatile char *)na != 'b' || *(volatile char *)(na + 1) != 0) continue;
            u32 p, kids;
            float w, h;
            why = "pane b aún sin crear";
            if (!Rd(cache + idx * 4, p) || !HeapOrLinear(p) || !Readable(p, 0xC0) || !Rd(p + 0x10, kids) ||
                !RdF(p + 0x48, w) || !RdF(p + 0x4C, h)) break;
            why = "pane b distinto (no es el icono)";
            if (kids != 3 || fabsf(w - 20.f) > 0.5f || fabsf(h - 20.f) > 0.5f) break;
            why = "memoria no escribible";
            if (!Process::CheckAddress((p + 0xB7) & ~0xFFFu, MEMPERM_WRITE)) break;
            s_heroPane = p; s_heroSlot = cache + idx * 4;
            Log::Write("mapa: icono de cabeza del juego oculto (pane b en %08lX)", (unsigned long)p);
            break;
        }
        if (!s_heroPane && !s_heroLogged && now > 0) { s_heroLogged = true; Log::Write("mapa: icono de cabeza aún no: %s", why); }
    }
    if (!s_heroPane) return;
    // cada cuadro: el pane sigue siendo el mismo (el juego pudo rehacer el mapa en la misma dirección)
    u32 cur;
    float w;
    if (!Rd(s_heroSlot, cur) || cur != s_heroPane || !RdF(s_heroPane + 0x48, w) || fabsf(w - 20.f) > 0.5f) { s_heroOwner = 0; s_heroPane = 0; return; }
    volatile u8 *flags = (volatile u8 *)(s_heroPane + 0xB7);
    if (*flags & 1) *flags = (u8)(*flags & ~1u);
    // En este juego ocultar el padre NO oculta a los hijos (comprobado en vivo: "b" oculto y la cara "e" se seguía
    // viendo y moviendo). Se ocultan también sus hijos: lista enlazada en b+0x14 (nodos en hijo+4, siguiente en +0).
    u32 sent = s_heroPane + 0x14, node;
    if (!Rd(sent, node)) return;
    for (int k = 0; k < 6 && node && node != sent; k++) {
        u32 child = node - 4, parent;
        if (!HeapOrLinear(child) || !Readable(child, 0xC0) || !Rd(child + 0xC, parent) || parent != s_heroPane) break;
        if ((child & ~0xFFFu) != (s_heroPane & ~0xFFFu) && !Process::CheckAddress((child + 0xB7) & ~0xFFFu, MEMPERM_WRITE)) break;
        volatile u8 *cf = (volatile u8 *)(child + 0xB7);
        if (*cf & 1) *cf = (u8)(*cf & ~1u);
        if (!Rd(node, node)) break;
    }
}

bool AreaNav(Xform &xf, u64 now) {
    u32 owner = 0;
    bool covered = false;
    u32 li = FindAreaNav(owner, covered);
    if (!li) { if (s_nav.owner) { s_nav.owner = 0; s_npts = 0; } s_nav.cached.ok = false; xf = s_nav.cached; return false; }
    if (s_fly.owner) { s_fly.owner = 0; s_npts = 0; }
    HideHeroIcon(owner, li, now);                  // también tapado: al cerrar el menú no asoma la cabecita
    if (covered) { xf = s_nav.cached; xf.ok = false; return false; }
    return Track(s_nav, owner, li, NAV_LAST_PANE, xf, now);
}

} // namespace GameMaps
