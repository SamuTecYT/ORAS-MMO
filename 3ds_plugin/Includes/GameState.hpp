/*
 * GameState.hpp — ORAS MMO v13
 *
 * Lectura (SOLO LECTURA) del estado del jugador local y de la cámara real del juego.
 * Todo verificado EN VIVO en Omega Ruby v1.0 (Azahar) el 2026-10-04:
 *
 *  Bloque del jugador P = GM + 0x124; GM = [GS + 0x1C]; GS = [0x0062F7C4] (notas de investigación §2.1, verificado
 *  2026-10-15). El escaneo por firma queda solo de respaldo si la cadena no se puede leer.
 *  Estado (KB §8.2): evento activo ev = [[GS+0x18]], cadena de padres en ev+4. Combate = EventBattleDll o
 *  EventBattleCall en la cadena; cargando = Fieldmap [GS+0x48]==0 o EventMapChangeCore; libre = ev==0.
 *    P+0x00 u16 zona actual (0..535)          P+0x02 u8 orientación (0 N,1 S,2 O,3 E,4 NO,5 NE,6 SO,7 SE)
 *    P+0x04 u32 montura (0 a pie, 1 Bici Carrera, 2 Bici Acrobática)
 *    P+0x10 f32 X   P+0x14 f32 altura   P+0x18 f32 Y      (18 unidades = 1 casilla)
 *    v1.0: P = 0x08C6A894 · v1.4: P = 0x08C6E884 (los datos de guardado se desplazan 0x3FF0)
 *  Copia "Situation" en P-0xE4: +0x00 u16 ESTADO (2 libre, 0xC002 menú/diálogo/combate/puerta, 3 cargando).
 *  Combate: u32 0x081FB478 == 0x40001 (0x40000 fuera).
 *  Cámara: matriz view-projection 4x4 (fila mayor) seguida de fov (+0xA4) y aspecto 1.6667 (+0xA8)
 *    y el objetivo (+0xB4). clip = M * (x, altura, y, 1). Se localiza por firma.
 *
 * Ya NO hay calibración (Paso 1 / Paso 2): las direcciones se encuentran solas en cada sesión.
 */
#pragma once
#include <3ds.h>
#include "Math3D.hpp"

namespace Game {

enum PlayerState : u8 {
    STATE_UNKNOWN = 0,
    STATE_FREE,          // caminando libremente por el mapa
    STATE_LOCKED,        // menú, diálogo, puerta (0xC002)
    STATE_LOADING,       // cambiando de mapa (3)
};

struct LocalState {
    bool  valid;          // hay jugador en el mapa (no título)
    u16   zone;
    float x, y, z;        // y = norte-sur, z = altura
    float yaw;            // radianes; 0 = sur (+y), +pi/2 = este (+x)
    u8    facing8;        // 0..7 tal cual el juego
    u8    ride;           // 0 a pie, 1/2 bici
    float speed;          // unidades/s
    u8    moveState;      // MoveState (Protocol.hpp)
    u8    playerState;    // PlayerState
    bool  inBattle;
    bool  shortcutMenu;   // v16.1: menú Y (objetos registrados) abierto: el mapa sigue a la vista (cuenta como libre)
    bool  fieldMenu;      // v16.5: menú X abierto (va en la pantalla de abajo; arriba sigue el mapa): cuenta como libre
    bool  inApp;          // v16.5: Mochila, Pokédex, Equipo, mapa grande...: el juego quita el mapa (playerState =
                          // STATE_LOADING) pero el jugador sigue en su sitio: los amigos lo siguen viendo ahí
    u64   stamp;
};

// v16.1: vtable (relativa a la base de DllField) del evento del menú Y, field::EventCallShortcutButton
// (tabla de vtables de eventos de las notas de investigación: 0x27107800 - 0x27000000)
static const u32 DLLFIELD_LIT_ADDR  = 0x0019C354;   // literal del stub -> DllField + 0x8EE38 (0x50DDA8 = sin cargar)
static const u32 DLLFIELD_LIT_OFF   = 0x0008EE38;
static const u32 DLLFIELD_UNLOADED  = 0x0050DDA8;
static const u32 VT_SHORTCUT_OFF    = 0x00107800;
// v16.5: menú X = evento EventSubScreen::EventFieldMenu (vtable fija de code.bin, KB §8.2 y §13.9)
static const u32 VT_FIELDMENU       = 0x005DED7C;

struct CameraState {
    bool  valid;          // false durante cargas/combates o si no se encontró
    Mat44 viewProj;       // clip = viewProj * (x, altura, y, 1)
    float fov;
};

void       Update(u64 nowMs);     // ~30 veces/s desde UN hilo (red)
LocalState Snapshot();            // copia protegida para otros hilos
// Relee combate y estado directamente de la memoria (para el OSD: la copia puede ser vieja si el
// hilo de red está ocupado conectando).
void       RefreshFlags(LocalState &st);
// Relee zona y posición en el instante del dibujo (sin el retraso del hilo de red): mi propio avatar
// queda exactamente sobre el modelo del juego.
void       RefreshPos(LocalState &st);
bool       ReadCamera(CameraState &out);   // llamar desde el OSD (hilo del juego) cada cuadro
u32        PlayerBlockAddr();     // 0 si aún no se encontró (diagnóstico)
u32        CameraAddr();

// Proyecta un punto de mundo a la pantalla superior (400x240). false si está detrás de la cámara.
bool       Project(const CameraState &cam, const Vec3 &world, float &sx, float &sy, float &w);

} // namespace Game
