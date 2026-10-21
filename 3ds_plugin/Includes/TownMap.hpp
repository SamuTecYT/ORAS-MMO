/*
 * TownMap.hpp — ORAS MMO v13
 * Zonas de Hoenn: zona+posición -> punto exacto de la imagen del mapa (poke_navi_map 380x270, tabla
 * HoennZones.hpp: 536 zonas, interiores/cuevas en su puerta), nombres y pines. v16: el mapa ya no se dibuja
 * (los amigos van sobre los mapas del propio juego: GameMaps).
 * Portable (sin CTRPF) para probarlo en PC.
 */
#pragma once
#include <stdint.h>
#include "HeroAssets.hpp"   // ByteSource
#include "Render3D.hpp"     // Target / PutPixel

namespace TownMap {

static const int IMG_W = 380, IMG_H = 270;

// Zona + posición -> píxel de la imagen 380x270 del mapa
bool ZoneToImage(uint16_t zone, float x, float y, float &px, float &py);

// Nombre del lugar (UTF-8, español) de una zona; "" si no se conoce
const char *ZoneNameEs(uint16_t zone);
// v17: nombre del lugar en el idioma del MMO (el oficial del juego: GameText; si aún no está, el de la tabla)
const char *ZoneName(uint16_t zone);
// v17: para el OSD (letra latina): en japonés/coreano, el inglés; si no, el del idioma (el OSD quita los acentos)
const char *ZoneNameOsd(uint16_t zone);

// v16: ruta marina / bajo el agua / fuera de la tabla (para no teletransportar a nadie al agua)
bool IsWaterZone(uint16_t zone);

// ¿Dos jugadores comparten el mismo espacio 3D? (misma zona, o misma matriz exterior)
bool SameSpace(uint16_t zoneA, uint16_t zoneB);

// Pin redondo con borde (lo usa la superposición)
void DrawPin(const R3D::Target &t, int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b);

} // namespace TownMap
