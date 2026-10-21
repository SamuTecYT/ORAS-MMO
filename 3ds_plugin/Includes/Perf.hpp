/*
 * Perf.hpp — ORAS MMO v16
 * Mide los cuadros reales del juego (una llamada por cuadro de la pantalla de arriba) para:
 *   - el log de diagnóstico: fps, "picos" (cuadros de más de 50 ms = tirón visible) y el peor cuadro;
 *   - la calidad automática: Level() 0 = va bien, 1 = va justo, 2 = va mal (con histéresis).
 * FrameMark lo llama el hilo del juego; Window()/Level() se leen desde otros hilos (contadores de 32 bits,
 * una lectura algo vieja no importa).
 */
#pragma once
#include <3ds.h>

namespace Perf {

struct Window { u32 frames, spikes, worstMs, ms; };

struct State {
    u64 last = 0, winStart = 0;
    Window cur = {0, 0, 0, 0}, done = {0, 0, 0, 0};
    int level = 0, badWins = 0, goodWins = 0;
    int forced = -1;            // -1 automático, 0 calidad, 2 ahorro
};

inline State &S() { static State s; return s; }

// Ventanas de 1 s: < 24 cuadros dos veces seguidas -> sube un nivel; >= 26 tres veces -> baja uno.
// v16.3: un cuadro de más de 250 ms es una CARGA del juego (puerta, mapa nuevo, guardar), no falta de potencia: se
// descarta como una pausa. Antes contaban, el nivel subía a "ahorro" al entrar a cualquier mapa y, como el juego va a
// 27-30 cuadros, casi nunca volvía a bajar (hacía falta >= 28 cuatro veces seguidas): los Pokémon de los amigos se
// ocultaban y aparecían a ratos (prueba con 4 amigos, 2026-10-18).
inline void FrameMark(u64 nowMs) {
    State &s = S();
    if (s.last && nowMs - s.last > 250) {            // pausa o carga: no cuenta (se empieza otra ventana)
        s.last = nowMs; s.winStart = nowMs; s.cur = Window{0, 0, 0, 0};
        return;
    }
    if (s.last) {
        u32 dt = (u32)(nowMs - s.last);
        s.cur.frames++;
        if (dt > 50) s.cur.spikes++;
        if (dt > s.cur.worstMs) s.cur.worstMs = dt;
    } else {
        s.winStart = nowMs;
    }
    s.last = nowMs;
    if (nowMs - s.winStart >= 1000) {
        s.cur.ms = (u32)(nowMs - s.winStart);
        u32 fps = s.cur.frames * 1000 / (s.cur.ms ? s.cur.ms : 1);
        if (fps < 24) { s.badWins++; s.goodWins = 0; } else if (fps >= 26) { s.goodWins++; s.badWins = 0; } else { s.badWins = 0; s.goodWins = 0; }
        if (s.badWins >= 2 && s.level < 2) { s.level++; s.badWins = 0; }
        if (s.goodWins >= 3 && s.level > 0) { s.level--; s.goodWins = 0; }
        // acumulado para el log (se vacía al leerlo)
        s.done.frames += s.cur.frames; s.done.spikes += s.cur.spikes; s.done.ms += s.cur.ms;
        if (s.cur.worstMs > s.done.worstMs) s.done.worstMs = s.cur.worstMs;
        s.cur = Window{0, 0, 0, 0};
        s.winStart = nowMs;
    }
}

// Resumen desde la última llamada (para la línea "diag" del log)
inline Window TakeWindow() { State &s = S(); Window w = s.done; s.done = Window{0, 0, 0, 0}; return w; }

inline void SetForced(int f) { S().forced = f; }
inline int  Level() { const State &s = S(); return s.forced >= 0 ? s.forced : s.level; }

} // namespace Perf
