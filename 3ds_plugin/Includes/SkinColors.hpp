/*
 * SkinColors.hpp — ORAS MMO v16.5
 * Colores de la ropa, los MISMOS en todas partes: tu textura real (SkinTex), tus amigos dibujados por el plugin
 * (Render3D), la vista previa de "Mi personaje" y el paquete de red.
 *  - Ropa: índice 0..15 (0 = original, la ropa roja tal cual) en el nibble bajo de lvls[3]. Los clientes v16.3
 *    solo conocen 0..8 y dibujan los nuevos (9..15) con la ropa original: nunca leen fuera de su tabla.
 *  - Gorro (Bruno) / lazo (Aura): byte btype de los paquetes POSITION (sin uso hasta v16.3, siempre 0):
 *    0 = como la ropa (lo de siempre), 1 = original, 2..16 = color 1..15.
 * El tinte conserva el sombreado: nuevo = color * intensidad del rojo original (5 bits por canal).
 */
#pragma once
#include <stdint.h>

namespace SkinColors {

static const int COUNT     = 16;          // colores de ropa (0 = original)
static const int ACC_COUNT = COUNT + 1;   // valores del gorro / lazo (0 = como la ropa)

static const uint8_t kTint[COUNT][3] = {
    {31, 0, 0},   {6, 12, 31},  {6, 28, 8},   {20, 8, 30},  {31, 28, 4},  {31, 12, 24}, {7, 7, 9},    {30, 30, 30},
    {4, 28, 31},  {31, 15, 2},  {3, 5, 17},   {17, 31, 3},  {17, 2, 7},   {15, 8, 3},   {22, 16, 31}, {16, 16, 18},
};

inline const char *Name(int v) {
    static const char *const n[COUNT] = { "Original", "Azul", "Verde", "Morado", "Amarillo", "Rosa", "Negro", "Blanco",
                                          "Cian", "Naranja", "Marino", "Lima", "Granate", "Marrón", "Lila", "Gris" };
    return n[(v >= 0 && v < COUNT) ? v : 0];
}

// v16.6/17.0: colores de la placa con tu nombre (nibble alto de lvls[3]). 0 = el automático de tu número de jugador;
// 1..8 como hasta v16.3 (los v16.3 dibujan 9..15 con el automático); 9..15 nuevos, los mismos nombres que la ropa.
static const int TAG_COUNT = 16;
static const uint8_t kTagRgb[TAG_COUNT][3] = {
    {0, 0, 0},       {70, 130, 255}, {60, 190, 90},  {160, 90, 230}, {240, 190, 40}, {240, 100, 170}, {70, 72, 84},
    {235, 235, 240}, {40, 200, 220}, {255, 140, 40}, {40, 64, 160},  {160, 230, 50}, {160, 30, 64},  {150, 96, 52},
    {196, 164, 255}, {34, 34, 40},
};
inline const char *TagName(int v) {
    static const char *const n[TAG_COUNT] = { "Automático", "Azul", "Verde", "Morado", "Amarillo", "Rosa", "Gris", "Blanco",
                                              "Cian", "Naranja", "Marino", "Lima", "Granate", "Marrón", "Lila", "Negro" };
    return n[(v >= 0 && v < TAG_COUNT) ? v : 0];
}

// Bit extra del color "efectivo" de una textura: recolorear también el blanco (el gorro de Bruno es blanco)
static const uint32_t EFF_WHITE = 0x10;

// Color efectivo de la textura de la cabeza (gorro / lazo). El cuerpo usa siempre el color de la ropa.
inline uint32_t HeadEff(uint32_t clothes, uint32_t acc, bool male) {
    if (clothes >= (uint32_t)COUNT) clothes = 0;
    if (acc == 0 || acc >= (uint32_t)ACC_COUNT) return clothes;   // como la ropa: solo lo rojo (como hasta v16.3)
    if (acc == 1) return 0;                                       // original
    return (acc - 1) | (male ? EFF_WHITE : 0);                    // Bruno: su gorro blanco; Aura: su lazo rojo
}

}  // namespace SkinColors
