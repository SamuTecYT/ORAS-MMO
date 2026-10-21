/*
 * GameMaps.hpp — ORAS MMO v16
 * Mapa de VUELO del propio juego (DllTownmap.cro): saber si está abierto y dónde dibuja el juego cada punto,
 * para poner a los amigos encima con precisión (desplazamiento y zoom incluidos). SOLO LECTURA.
 *
 * Cadena (decompilado 2026-10-16, KB §13.4):
 *   DllTownmap cargado: literal del stub de importación 0x0019C124 != 0x0050DDA8; base = valor - 0x9654
 *   proceso activo: GS+0x14 (GameProcManager) -> +0x10 -> +0xC == Proc (vtable base+0xF488)
 *   Proc+0x24 MainSystem -> +0x18 GraphicWork -> +0xA4 MapView (vtable base+0xF4B4)
 *   MapView+0x5C LayoutWork -> +0x1C[0] layout -> +4 instancia: +0xC8 caché de panes, +0xBC->+8 tabla de nombres
 *   pane: matriz global 3x4 en +0x80 (traslación x +0x8C, y +0x9C), origen en el centro de la pantalla, +Y arriba
 * El diseño es map_lower.bclyt (el mismo del AreaNav): con los nombres de los panes y MapLowerPanes.hpp se ajusta
 * imagen (380x270) -> pantalla con todos los puntos que el juego tiene colocados.
 */
#pragma once
#include <3ds.h>

namespace GameMaps {

// pantalla_x = ox + s * img_x ; pantalla_y = oy + s * img_y   (píxeles de la pantalla táctil 320x240)
struct Xform { bool ok; float s, ox, oy; int pairs; float err; };

// true si el mapa de Vuelo está abierto ahora; 'xf' con la transformación (xf.ok = false si aún no cuadra)
bool FlyMap(Xform &xf, u64 nowMs);

// v16: lo mismo para el AreaNav del PokéNav (pantalla táctil). Sin mirar píxeles: por sus objetos (KB §13.5)
bool AreaNav(Xform &xf, u64 nowMs);

} // namespace GameMaps
