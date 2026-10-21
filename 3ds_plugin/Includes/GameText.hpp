/*
 * GameText.hpp — ORAS MMO v17.0
 * Nombres OFICIALES de Pokémon y de lugares en el idioma del MMO, leídos de los textos del propio juego del jugador
 * (RomFS /a/0/7/N, N = 1..8: japonés kana, japonés kanji, inglés, francés, italiano, alemán, español, coreano;
 * archivo 98 = especies, 90 = lugares; formato de texto de 6.ª generación, cifrado con la clave 0x7C89).
 * El plugin no incluye textos del juego: los saca de la copia de cada uno, como los modelos 3D.
 */
#pragma once
#include <3ds.h>
#include "HeroAssets.hpp"

namespace GameText {

// Hilo de recursos, al arrancar: los nombres de los 8 idiomas (~0,3 s; se quedan en memoria). Así, al cambiar de
// idioma en el menú, los nombres cambian al momento y GameFont puede preparar las letras de japonés y coreano antes.
void Preload(Hero::ByteSource &romfs);
// Hilo de recursos: carga (o recarga si cambió el idioma) los nombres. Barato si no hay nada que hacer.
void Serve(Hero::ByteSource &romfs);

const char *Species(u16 id);       // UTF-8 en el idioma que se ve (Lang::Shown); nullptr si aún no está (usar el de respaldo)
const char *Location(u16 locId);   // id de lugar del juego (HoennZones: location_id)
const char *SpeciesEn(u16 id);     // inglés (para el OSD, que solo tiene letras latinas)
const char *LocationEn(u16 locId);
const char *SpeciesOf(int gameIndex, u16 id);    // de un idioma concreto (1..8), si está cargado. Lo usa GameFont
const char *LocationOf(int gameIndex, u16 locId);
int  Ready();                      // índice de textos ya cargado (1..8; 0 = ninguno)

}
