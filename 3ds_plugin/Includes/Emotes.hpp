/*
 * Emotes.hpp -- ORAS MMO v14
 * Emotes dibujados en pixel art propio (12x12), se muestran en una burbuja sobre el jugador.
 * Independiente de CTRPF: el dibujo se hace con una función de "rectángulo" que pasa quien llama.
 */
#pragma once

namespace Emotes {
static const int COUNT = 14;
extern const char *const kNames[COUNT];
// put(user, x, y, tamaño, r, g, b): rellena un cuadrado de tamaño x tamaño
typedef void (*PixelFn)(void *user, int x, int y, int size, unsigned char r, unsigned char g, unsigned char b);
void Draw(int idx, int x, int y, int scale, PixelFn put, void *user);
}
