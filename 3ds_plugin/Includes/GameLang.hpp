/*
 * GameLang.hpp — ORAS MMO v17.0
 * Idioma del JUEGO (textos y fuentes del propio Rubí Omega / Zafiro Alfa) elegido desde el menú del MMO.
 *
 * Cómo lo hace el juego (KB §13.12, descompilado y comprobado en vivo): al arrancar, EventLangSelect lee el idioma de
 * la partida (MyStatus, GameData+0x129D5) y llama a SetLanguage(idioma, heap, kanji) (0x33A440), que guarda el idioma
 * en un byte global (*(0x62F7D0)), elige los textos (FUN_0010b468) y recarga las fuentes (FUN_00107518). Es la misma
 * función que usa la pantalla "elige tu idioma" de una partida nueva, así que es un camino normal del juego.
 *
 * El plugin la engancha ANTES de que arranque el juego (PatchProcess) y cambia solo el idioma que recibe: la partida
 * guardada NO cambia (su idioma sigue igual; quitar el plugin o elegir "Automático" deja el juego como estaba). Se
 * aplica al reiniciar el juego: cambiarlo con menús abiertos dejaría textos y fuentes a medias.
 * Códigos (los del juego): 1 japonés (kana), 2 inglés, 3 francés, 4 italiano, 5 alemán, 7 español, 8 coreano;
 * del plugin: 0 = automático (el de la partida), 9 = japonés con kanji.
 */
#pragma once
#include <3ds.h>

namespace GameLang {

static const int AUTO = 0, JP_KANJI = 9;

void Early();               // desde PatchProcess (antes del juego): comprueba la build y engancha
void LogStatus();           // desde main, con el registro ya abierto: qué pasó al arrancar
bool Hooked();
int  SaveLang();            // idioma que pidió la partida al arrancar (0 = aún no / desconocido)
int  Current();             // idioma real del juego ahora (byte global; 0 = aún no)
bool CurrentKanji();        // japonés con kanji
int  Applied();             // idioma que el plugin puso al arrancar (0 = ninguno: automático)

}
