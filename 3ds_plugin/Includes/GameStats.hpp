/*
 * GameStats.hpp — ORAS MMO v14
 * Lectura (SOLO LECTURA) del progreso del jugador para el ranking de la sala y del combate en curso
 * para que los amigos lo vean desde fuera. Todo es relativo al bloque del jugador P (GameState), así que
 * vale para cualquier versión del juego en la que P se encuentre.
 *
 *  - Dinero / medallas: bloque Misc (PKHeX Misc6AO: dinero +0x08, medallas +0x0C) = P + 0x3534
 *    (v1.0: medallas en 0x08C6DDD4, igual que Citra-Tracker).
 *  - Equipo: P + 0x8C9E8 (v1.0 0x08CF727C), 6 huecos cada 0x1E4; PK6 cifrado (LCRNG + barajado de
 *    bloques) y sus estadísticas de equipo (+0x158, 22 bytes) cifradas con el mismo PV.
 *  - Tiempo de juego y Pokédex: el plugin los LOCALIZA solo (firmas: el tiempo avanza; la Pokédex tiene
 *    al inicial capturado) y los valida antes de usarlos.
 *  - Combate: copias de combate del equipo propio y del rival + estructura con nivel y PS en vivo
 *    (direcciones relativas al equipo, como Citra-Tracker).
 */
#pragma once
#include <3ds.h>
#include "RankScore.hpp"

namespace Stats {

struct Mine {
    bool okTime, okDex, okParty, okMisc;
    u16  hours;  u8 minutes;
    u8   badges;                 // bits: medalla i
    u16  caught, seen;
    u32  money;
    u8   partyCount, avgLevel, maxLevel;
    u16  leadSpecies;
    bool league;                 // v16: Salón de la Fama con al menos una entrada
};

struct Battle {
    u8   kind;                   // 0 nada, 1 salvaje, 2 entrenador
    u16  mySpecies; u8 myLevel; u16 myHp, myHpMax;
    u16  opSpecies; u8 opLevel; u16 opHp, opHpMax;
};

void   Update(u64 nowMs, bool inBattle);    // hilo de red (barato; lo pesado se reparte en el tiempo)
Mine   GetMine();
Battle GetBattle();
Rank::Input ToRank(const Mine &m);
int    Score(const Mine &m);                // puntos del ranking (RankScore.hpp)

} // namespace Stats
