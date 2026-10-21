/*
 * FriendMotion.hpp — ORAS MMO v16.1
 * "Físicas" de los amigos, IGUALES para el modelo real del juego (GameHook) y para el dibujo propio del plugin
 * (Overlay: en bici, con ropa de color...). Antes solo el modelo real las tenía y al subir a la bici o cambiar
 * de color el amigo "perdía las físicas" (se metía encima de ti y se movía a tirones).
 *  - Nunca encima de ti: si por el retraso de la red su posición cae en tu sitio, se queda justo al lado.
 *  - Movimiento continuo: cada cuadro recorre la mitad del camino que le falta (sin saltos al llegar paquetes
 *    ni al esquivarte). Más de 2 casillas de golpe (puerta, viaje, reconexión) = se coloca directo.
 */
#pragma once
#include <cmath>

namespace FriendMotion {

static const float MIN_SEP = 15.f;   // separación mínima con tu personaje (casilla = 18)
static const float SNAP    = 36.f;   // distancia a partir de la cual se coloca directo
static const float FOLLOW  = 0.5f;   // fracción del camino que recorre en cada cuadro

struct State { bool has; float x, y; };

// (tx, ty) = dónde está según la red (suelo: x, y); (mx, my) = tu personaje (hasMe = false: no se sabe)
inline void Step(State &s, float tx, float ty, bool hasMe, float mx, float my) {
    if (hasMe) {
        float dx = tx - mx, dy = ty - my, d = sqrtf(dx * dx + dy * dy);
        if (d < MIN_SEP && std::isfinite(d)) {
            if (d < 0.5f) { dx = 0.f; dy = 1.f; d = 1.f; }               // justo encima: hacia el sur
            tx = mx + dx / d * MIN_SEP; ty = my + dy / d * MIN_SEP;
        }
    }
    if (!s.has || fabsf(tx - s.x) + fabsf(ty - s.y) > SNAP) { s.x = tx; s.y = ty; s.has = true; }
    else { s.x += (tx - s.x) * FOLLOW; s.y += (ty - s.y) * FOLLOW; }
}

} // namespace FriendMotion
