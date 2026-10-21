/*
 * GameFont.hpp — ORAS MMO v17.0
 * Letras del menú en japonés y coreano con la FUENTE DEL PROPIO JUEGO (RomFS /a/1/6/7: entrada 1 = kana + kanji,
 * entrada 0 = kana + hangul; mismo formato que la fuente del sistema: celdas 24x30, A4, hojas de 128x32).
 * Por qué: la fuente del sistema de la consola no trae el coreano, y la que pone Azahar cuando no se volcó la de la
 * consola no tiene la "ん". Solo se preparan las letras que el menú puede necesitar (sus textos, los nombres de
 * Pokémon y lugares del juego en ese idioma y el ASCII), ya reducidas al tamaño con el que CTRPF dibuja la fuente del
 * sistema (14x16, 4 bits por punto): unos 120 bytes por letra. Se preparan una vez por idioma y no se liberan nunca
 * (el menú puede estar dibujando con ellas). Una letra que no esté se dibuja con la fuente del sistema.
 * v17.0 r2: las de los DOS idiomas se preparan al arrancar (~2 s en el hilo de recursos, ~130 KB): antes se preparaban
 * al elegir el idioma y durante unos segundos el menú salía con "?". Mientras no estén, Lang::Shown() da el inglés.
 */
#pragma once
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include "HeroAssets.hpp"
#include "Lang.hpp"

namespace GameFont {

void Serve(Hero::ByteSource &romfs);   // hilo de recursos: prepara las letras de un idioma por llamada (primero el elegido)
bool Ready(Lang::Id id);               // las letras de ese idioma (japonés o coreano) ya están
bool Active();                         // el idioma elegido es japonés/coreano y sus letras ya están
u32  Generation();                     // cambia cuando cambia la fuente activa (para olvidar anchos ya medidos)
int  Width(const char *utf8);          // ancho en píxeles
void Draw(const CTRPluginFramework::Screen &s, const char *utf8, int x, int y, const CTRPluginFramework::Color &c, int maxX);

}
