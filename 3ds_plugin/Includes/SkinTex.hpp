/*
 * SkinTex.hpp — ORAS MMO v14.5
 * "Skin perfecta": recolorea la ropa roja DIRECTAMENTE en la textura del personaje que usa el juego, así el
 * modelo real (con su animación, su bici, sus poses) sale con tu color sin desfase alguno.
 *
 * Excepción deliberada del proyecto a la regla de "solo leer la memoria del juego": solo se tocan
 * píxeles de las texturas del protagonista (nunca lógica, partidas ni datos). Se guarda el original y se
 * restaura al elegir "Original", al desactivarlo o si el juego recarga la textura.
 *
 * Búsqueda: copia exacta de los bytes originales (los conocemos por HeroAssets) en la VRAM y en la memoria
 * lineal del juego, en segundo plano y por tramos para no provocar tirones.
 * v16.5: 16 colores y color propio del gorro / lazo (SkinColors.hpp). Cada 250 ms se comprueba cada copia en las
 * palabras donde la pintada y la original difieren: si el juego la recarga (menús, combates, mapas), se repinta.
 */
#pragma once
#include <3ds.h>

namespace SkinTex {
void Update(u64 nowMs);      // hilo de red: buscar / aplicar / verificar / restaurar
bool Active();               // true si la textura del juego ya tiene tu color (no hace falta dibujar encima)
void RestoreAll();           // al salir
}
