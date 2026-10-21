/*
 * RankScore.hpp — ORAS MMO v16
 * Puntos del ranking por niveles: lo de arriba SIEMPRE pesa más que todo lo de abajo junto.
 *   1000 por cada medalla y 1000 por la Liga (Salón de la Fama)  -> 0..9000
 *   Pokédex: capturados * 600 / 721                               -> 0..600
 *   Equipo: 2 * nivel máximo + 2 * nivel medio (tope 399)         -> 0..399
 * Un dato desconocido (no se pudo leer) vale 0: nunca sube ni hunde a nadie.
 * El tiempo de juego y el dinero se muestran pero no puntúan (no miden progreso).
 * Funciones puras constexpr: los static_assert del final son las pruebas (las corre el compilador).
 */
#pragma once
#include <stdint.h>

namespace Rank {

struct Input {
    uint8_t  badgeMask;      // bit i = medalla i
    bool     league;         // Salón de la Fama con al menos una entrada
    bool     okDex;  uint16_t caught;
    bool     okParty; uint8_t maxLevel, avgLevel;
};

struct Parts { int badges, league, dex, team, total; };

// (C++11: el proyecto compila con -std=gnu++11 -> constexpr de una sola expresión)
constexpr int BadgeCount(uint8_t m) { return m ? (m & 1) + BadgeCount((uint8_t)(m >> 1)) : 0; }
constexpr int Min(int a, int b) { return a < b ? a : b; }
constexpr int BadgePts(const Input &in) { return BadgeCount(in.badgeMask) * 1000; }
constexpr int LeaguePts(const Input &in) { return in.league ? 1000 : 0; }
constexpr int DexPts(const Input &in) { return in.okDex ? Min(in.caught, 721) * 600 / 721 : 0; }
constexpr int TeamPts(const Input &in) { return in.okParty ? Min(2 * in.maxLevel + 2 * in.avgLevel, 399) : 0; }
constexpr int Total(const Input &in) { return BadgePts(in) + LeaguePts(in) + DexPts(in) + TeamPts(in); }
constexpr Parts Score(const Input &in) { return Parts{ BadgePts(in), LeaguePts(in), DexPts(in), TeamPts(in), Total(in) }; }

// ¿Quién va delante? <0: a, >0: b, 0: empate. Desempate: puntos, medallas, capturados, nivel máximo
// (el nombre lo decide quien ordena)
constexpr int Cmp(int x, int y) { return x == y ? 0 : (x > y ? -1 : 1); }
constexpr int Compare(const Input &a, const Input &b) {
    return Cmp(Total(a), Total(b)) ? Cmp(Total(a), Total(b))
         : Cmp(BadgeCount(a.badgeMask), BadgeCount(b.badgeMask)) ? Cmp(BadgeCount(a.badgeMask), BadgeCount(b.badgeMask))
         : Cmp(a.okDex ? a.caught : 0, b.okDex ? b.caught : 0) ? Cmp(a.okDex ? a.caught : 0, b.okDex ? b.caught : 0)
         : Cmp(a.okParty ? a.maxLevel : 0, b.okParty ? b.maxLevel : 0);
}

// ---- pruebas (partidas de ejemplo: 8, 6 y 3 medallas) ----
namespace test {
constexpr Input alex    { 0xFF, false, true, 258, true, 50, 44 };
constexpr Input blair   { 0x3F, false, true, 356, true, 49, 41 };
constexpr Input casey   { 0x07, false, true, 365, true, 31, 25 };
constexpr Input oneMoreBadge { 0x01, false, true, 0,   true, 5, 5 };
constexpr Input fullDex      { 0x00, false, true, 721, true, 100, 100 };
constexpr Input unknownDex   { 0x07, false, false, 700, true, 31, 25 };
constexpr Input champion     { 0xFF, true,  true, 200, true, 60, 50 };
}
static_assert(Total(test::alex) > Total(test::blair), "8 medallas por delante de 6");
static_assert(Total(test::blair) > Total(test::casey), "6 medallas por delante de 3");
static_assert(Total(test::oneMoreBadge) > Total(test::fullDex), "1 medalla gana a toda la Pokedex y nivel 100");
static_assert(Score(test::unknownDex).dex == 0, "Pokedex desconocida = 0");
static_assert(Total(test::champion) > Total(test::alex), "la Liga cuenta como una medalla mas");
static_assert(Score(test::fullDex).dex == 600 && Score(test::fullDex).team == 399, "topes");
static_assert(Score(test::alex).total == 8000 + 258 * 600 / 721 + 2 * 50 + 2 * 44, "suma exacta");
static_assert(Compare(test::alex, test::blair) < 0 && Compare(test::blair, test::alex) > 0 && Compare(test::alex, test::alex) == 0, "orden");

} // namespace Rank
