/*
 * Lang.hpp — ORAS MMO v17.0
 * Idioma del MMO (menú SELECT, avisos, etiquetas). Por defecto el del juego: el de la partida (o el elegido en el menú,
 * que también cambia el juego: GameLang.hpp). Los textos se escriben en español en el código y se traducen con TR():
 * la tabla está en LangTable.inc, GENERADA por tools/i18n/gen_lang.py desde tools/i18n/strings.py (no editar a mano;
 * el generador comprueba que cada traducción tenga los mismos %s/%d que el original).
 * Nombres de Pokémon y de lugares: los del propio juego en cada idioma (GameText.hpp).
 */
#pragma once
#include <3ds.h>

namespace Lang {

enum Id : u8 { ES = 0, EN, FR, IT, DE, JA, KO, COUNT };

void Init();                     // arma la tabla (una vez, al arrancar, antes de los hilos)
Id   Ui();                       // idioma del MMO elegido ahora
Id   Shown();                    // idioma que se VE: el elegido, o el inglés mientras japonés/coreano no tienen sus letras
                                 // preparadas (GameFont), para no dibujar nunca "?" (solo en los primeros segundos)
bool Cjk();                      // se ve en japonés o coreano (la letra pequeña del OSD no los tiene: allí, el inglés)
Id   FromGame(int gameCode);     // 1/9 ja, 2 en, 3 fr, 4 it, 5 de, 7 es, 8 ko (otros: es)
int  GameIndex(Id id);           // índice de los textos del juego (/a/0/7/N, N = 1..8)
const char *T(const char *es);   // traducción (el original si no está en la tabla)
const char *TOsd(const char *es);// para el OSD (letra ASCII): en japonés/coreano, el inglés
const char *Name(int gameCode);  // nombre de un ajuste de idioma en su propio idioma ("English", "日本語"...)
int  Rows();                     // textos de la tabla (GameFont prepara sus letras)
const char *Row(int i, Id id);   // texto i en ese idioma (el inglés si no tiene)

}

#define TR(s) Lang::T(s)
