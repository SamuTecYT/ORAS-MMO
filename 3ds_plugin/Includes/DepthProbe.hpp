/*
 * DepthProbe.hpp — ORAS MMO v17.0
 * Profundidad REAL de la escena de la pantalla de arriba, para que lo que dibuja el plugin (acompañantes, amigos de
 * color o en bici) quede tapado por tejados, árboles y paredes que están delante, como los modelos del propio juego.
 *
 * Por qué hace falta un gancho (KB §13.12): el juego dibuja las dos pantallas en el MISMO lienzo de la GPU (256x512 en
 * la VRAM): primero el mundo 3D de arriba, lo copia a la pantalla (DisplayTransfer), BORRA color y profundidad
 * (MemoryFill) y dibuja la de abajo. Cuando el plugin dibuja (al cambiar de cuadro) la profundidad de arriba ya no
 * existe. El gancho está en la función del juego que añade órdenes a la cola de la GPU (0x1740C4, nn::gx): justo
 * cuando llega la copia de la pantalla de arriba, la profundidad todavía es la del mundo 3D y se copia aquí.
 * Solo LEE la memoria de vídeo; no cambia nada del juego. Se instala una vez al arrancar (como GameHook).
 */
#pragma once
#include <3ds.h>

namespace DepthProbe {

bool Install();                      // comprueba la build y engancha (una vez, al arrancar)
bool Active();

// El OSD pide cada cuadro la zona de la pantalla de arriba (400x240) donde va a dibujar algo que pueda quedar tapado
// (se captura el rectángulo que las contiene a todas). Sin zonas no se lee nada: leer la profundidad obliga a Azahar a
// esperar a la GPU una vez por cuadro.
struct Rect { s16 x0, y0, x1, y1; };
void Want(const Rect *r, int n);

// Profundidad de la escena (24 bits del juego; más pequeño = más cerca; 0xFFFFFF = nada, p. ej. el suelo del Centro
// Pokémon no la escribe) en el píxel (x, y) de la pantalla de arriba. false si (x, y) no está en lo capturado.
bool At(int x, int y, u32 &depth);
u32  Frame();                        // número de capturas hechas (cambia en cada cuadro con captura)
const u32 *Buffer();                 // 400x240 (solo vale dentro de Region)
bool Region(Rect &r);                // lo capturado en la última captura

// Pruebas: guardar la profundidad entera como imagen en el siguiente cuadro (mmo_dev_depth_N.bmp)
void RequestDump();
bool ServiceDump();                  // desde el OSD de arriba: escribe la imagen si está lista (true = la escribió)
// Estadísticas para el registro: capturas y ticks gastados en leer
void Stats(u32 &captures, u64 &ticks);

}
