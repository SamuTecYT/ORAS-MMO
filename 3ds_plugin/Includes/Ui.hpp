/*
 * Ui.hpp — ORAS MMO v14
 * Menú propio del MMO (se abre con SELECT en lugar del menú genérico de CTRPF).
 * Pausa el juego y dibuja una interfaz completa en ambas pantallas:
 *  - superior: barra lateral de secciones + panel de contenido (con vista previa 3D del personaje);
 *  - inferior: pestañas y botones grandes táctiles (ratón en Azahar).
 * Se maneja con cruceta/stick + A/B/L/R (teclado en Azahar) o tocando la pantalla inferior.
 */
#pragma once
#include <3ds.h>

namespace Ui {
// Llamar desde PluginMenu::OnOpening. Devuelve true solo si hay que abrir el menú avanzado de CTRPF.
bool OnMenuHotkey();
// Prueba de desarrollo: la próxima vez que se abra, el menú recorre solo todas sus secciones y se cierra
void StartDemo(int mode = 0);   // prueba: 0 = recorre las secciones, 1 = cambia el idioma muchas veces (v17)
}

// Acciones de red que implementa main.cpp (mismo formato de paquete que el resto del plugin)
void UiSendSimple(u8 type, u8 anim, u8 btype, const char *msg);
