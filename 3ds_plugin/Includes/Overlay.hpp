/*
 * Overlay.hpp — ORAS MMO v13
 * Todo lo que se dibuja sobre el juego (callback OSD de CTRPF, hilo del juego):
 *  - pantalla superior: avatares 3D reales de los amigos, nombres, bocadillos, indicadores de borde,
 *    HUD compacto y últimas líneas de chat;
 *  - pantalla táctil: mapa de Hoenn con la posición exacta de cada jugador (con el AreaNav abierto).
 */
#pragma once
#include <CTRPluginFramework.hpp>
#include "Render3D.hpp"

namespace Overlay {
bool Init();
bool OnFrame(const CTRPluginFramework::Screen &screen);
// Destino de dibujo (framebuffer rotado de la 3DS) a partir de una pantalla de CTRPF
// Muestra el HUD unos segundos (modo automático)
void PokeHud();
bool MakeTarget(const CTRPluginFramework::Screen &s, R3D::Target &t);
// Prueba de desarrollo (solo con mmo_dev_menu.txt): guarda lo que muestra la pantalla en un BMP de la SD
void DevShot(const CTRPluginFramework::Screen &s, const char *path);
// v16.2: true mientras OnFrame está dibujando (el menú espera a que termine antes de liberar memoria del motor)
bool Busy();
// v16.3: con true el OSD deja de dibujar (el menú SELECT lo pide ANTES de pausar el juego, para que ningún cuadro se
// quede a medias durante la pausa y se pueda liberar la memoria)
void Hold(bool hold);
}
