/*
 * Follower3D.hpp — ORAS MMO v16.3
 * Pokémon acompañantes "3D": cada especie es un render de su MODELO 3D del propio juego (pose de reposo de Poké
 * Recreo, vista con la cámara del mapa), en 5 direcciones (de frente, 3/4, de lado, 3/4 de espaldas, de espaldas; la
 * izquierda es el espejo) y 9 cuadros por dirección: 0 = quieto, 1..8 = ciclo de paso (patas, cadera, brazos y cola
 * con su esqueleto; el cuerpo sube y baja con cada paso; los que no tienen patas, su propio bucle de reposo). Vienen de
 * mmo_pokes_3d.bin ("MMO3D3" con LZMA, o el "MMO3D2" de la v16.2 con LZ11; tools/pokepack/make_3d_pack.py), que
 * se reparte solo en el ZIP privado (son gráficos del juego). v16.3: son los ÚNICOS dibujos de Pokémon del plugin (los
 * íconos viejos de 40x30 ya no existen): sin paquete, no hay acompañantes.
 *
 *  - El OSD pide (especie, dirección) con Begin() y End() al terminar. Si esa dirección aún no está, devuelve otra ya
 *    cargada de la misma especie (y la pide): solo se lee la dirección que se ve, en unas centésimas.
 *  - Memoria: un bloque pequeño por (especie, dirección) (≈16 KB; nada de un bloque grande que fragmente el montón),
 *    con tope total; lo que lleva medio segundo sin dibujarse se libera. Todo se libera al abrir el menú SELECT.
 *  - Draw(): decodifica el cuadro, lo reduce al tamaño de pantalla con supermuestreo (bordes limpios), le aplica la luz
 *    del ambiente (tinte) y oscurece 1 px de borde (como el contorno de los modelos del juego).
 */
#pragma once
#include <3ds.h>
#include "Render3D.hpp"

namespace Follower3D {

static const int DIRS = 5, MAXFRAMES = 9;

struct Dir { u8 w, h; s16 ax, ay; };       // tamaño horneado y pies (ancla) en el recorte

struct View;
// Hilo del OSD. nullptr = aún no hay ninguna dirección de esa especie (o no hay paquete): no se dibuja.
// Si devuelve algo, llamar a End() en el mismo cuadro: mientras tanto el hilo de recursos no toca esos datos.
const View *Begin(u16 species, int dir, u64 nowMs);
void End();
int  RefPx();                              // alto del entrenador en los píxeles horneados (72)
float RadiusPx(const View *v);             // mitad del ancho mayor de los cuadros de esa dirección (px horneados)
bool Walks(const View *v);                 // anda con patas (si no: flota/nada y sus cuadros son su reposo)
int  Frames();                             // cuadros por dirección del paquete (5 o 9): 0 = quieto, el resto el ciclo
float Stride(const View *v);               // con patas: unidades del mundo que avanza en un ciclo (0 = no se sabe)
int  CycleMs(const View *v);               // sin patas: duración de su bucle de reposo en ms (0 = no se sabe)

// Dibuja el cuadro f (0..Frames()-1) de la vista (mirror = mirando a la izquierda) con escala k (px de pantalla por px
// horneado), los pies en (fx, fy) y aplastamiento sqx/sqy. tint = luz del ambiente (256 = 1.0) por canal.
// holes = rectángulos {x0,y0,x1,y1} que no se pintan (el entrenador delante), nh = cuántos.
bool Draw(const R3D::Target &t, const View *v, int f, bool mirror, float k, float fx, float fy, float sqx, float sqy,
          const int tint[3], const int *holes, int nh, bool cheap = false);   // cheap: suavizado barato (modo ahorro)
// v17: rectángulo de pantalla del último Draw() (x1/y1 exclusivos): el OSD pide ahí la profundidad del cuadro siguiente
void LastRect(int &x0, int &y0, int &x1, int &y1);

// Cuadro quieto de 3/4 encajado en la caja (x, y, w, h), pies abajo y centrado (faceRight = mira a la derecha).
// false = aún no está cargado (ya se pidió: probar en el siguiente cuadro).
bool DrawFit(const R3D::Target &t, u16 species, int x, int y, int w, int h, bool faceRight, u64 nowMs);

// Hilo de recursos: lee lo pendiente de la SD. Barato si no hay nada que hacer. true = queda trabajo pendiente.
bool Serve();
// Coste del dibujo por etapas (ticks del ARM11; para el registro)
struct Prof { u32 draws, misses, px; u64 idx, pal, samp, edge, blit; };
extern Prof g_prof;
// Al arrancar el plugin: reserva el espacio propio de los dibujos (256 KB seguidos) antes de que se reparta la memoria.
void Reserve();
// Libera toda la memoria (al abrir el menú). Se vuelve a reservar sola cuando haga falta.
void Release();

} // namespace Follower3D
