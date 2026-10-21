/*
 * Render3D.hpp — ORAS MMO v13
 * Motor de avatares 3D por software: dibuja el MODELO REAL del protagonista (HeroAssets) con su
 * animación real, la cámara real del juego y el mismo estilo (toon + contorno).
 *
 * Pensado para la CPU de la 3DS: cada avatar se rasteriza en su propio sprite y en cada cuadro solo
 * se compone en el framebuffer. Se vuelve a rasterizar únicamente si cambió la pose o la vista, y un
 * presupuesto de ciclos por cuadro (autoajustable) evita que el juego pierda FPS.
 * Portable (sin CTRPF) para poder probarlo en PC.
 */
#pragma once
#include <stdint.h>
#include "Math3D.hpp"
#include "HeroAssets.hpp"

namespace R3D {

static const int MAX_SLOTS = 16;     // ids de avatar 1..16

// Formatos de framebuffer del GSP de la 3DS
enum FbFormat { FB_RGBA8 = 0, FB_BGR8 = 1, FB_RGB565 = 2, FB_RGB5A1 = 3, FB_RGBA4 = 4 };

struct Target {
    uint8_t *base;   // dirección del píxel (0,0)
    int      dx, dy; // bytes entre (x,y) y (x+1,y) / (x,y+1)  (pueden ser negativos)
    int      fmt;    // FbFormat
    int      w, h;   // 400 x 240 en la pantalla superior
    bool     track;  // (reservado)
    // v17: profundidad REAL de la escena (DepthProbe, 400x240, 24 bits del juego; menor = más cerca). Con zbuf, un
    // píxel no se pinta si en ese punto la escena está más cerca que zthr (un tejado, un árbol, alguien delante).
    // Solo dentro de [zx0,zx1)x[zy0,zy1), lo capturado en este cuadro; fuera se pinta como siempre.
    const uint32_t *zbuf = nullptr;
    uint32_t zthr = 0;
    int16_t  zx0 = 0, zy0 = 0, zx1 = 0, zy1 = 0;
    // v17.0 r2: estaba casi entero detrás de algo en el cuadro anterior (MostlyHidden): no se pinta, solo se cuenta
    // cuánto se vería (así no quedan "partículas" sueltas de algo que está detrás de un edificio)
    bool     zskip = false;
};

// v17.0 r2: lo que dio la prueba de profundidad en el último Blit565Key/BlitScaledKey: píxeles que se verían (fuera de
// los huecos) y cuántos tapaba la escena (0 y 0 si no había nada delante).
void LastDepth(int &total, int &hidden);
// ¿Casi entero detrás? Con histéresis para que no parpadee en el límite: se oculta con menos del 8 % a la vista y vuelve
// a salir con el 15 %.
static inline bool MostlyHidden(int total, int hidden, bool wasHidden) {
    if (total <= 0) return false;
    return (total - hidden) * 100 < total * (wasHidden ? 15 : 8);
}

// v17: umbral de profundidad (24 bits, como el juego: -z/w * 0xFFFFFF, medido en vivo) para algo con los pies en
// 'feet' (x este, altura, y sur: como Game::Project): lo que esté más cerca de la cámara que sus pies menos 'margin'
// unidades de mundo lo tapa. 0 = no se puede calcular (entonces no se prueba).
uint32_t DepthThreshold(const Mat44 &viewProj, const Vec3 &feet, float margin);
static const float DEPTH_MARGIN = 6.f;       // 1/3 de casilla: lo que está "al lado" no tapa


struct Instance {
    const Hero::Model *model;
    Vec3     pos;        // pies: x = este, y = norte-sur (+ sur), z = altura (como GameState)
    float    yaw;        // 0 = mira al sur (+y), +pi/2 = este (+x)
    int      anim;       // Hero::AnimId
    float    frame;      // cuadro (con decimales) dentro de la animación
    int      prevAnim;   // animación anterior para mezclar (o -1)
    float    prevFrame;
    float    blend;      // 0 = solo prevAnim, 1 = solo anim
    bool     blink;      // ojos cerrados este cuadro
    uint8_t  id;         // 1..MAX_SLOTS: sprite propio de cada avatar
    uint8_t  variant;    // 0 original, 1..15 colores (la ropa roja cambia de color; SkinColors.hpp)
    uint8_t  acc;        // v16.5: gorro / lazo: 0 como la ropa, 1 original, 2..16 color 1..15
    bool     urgent;     // re-rasterizar en cada cuadro sin esperar presupuesto (mi propio personaje)
    const Hero::Model *attach;   // v16: modelo que va con él (la bici), mismas anim/frame; nullptr = ninguno
};

struct DrawInfo {
    bool  drawn;
    int   minX, minY, maxX, maxY;   // rectángulo en pantalla
    float headX, headY;             // punto sobre la cabeza (para el nombre)
    float depth;                    // w de los pies (distancia a la cámara)
};

bool Init();                    // reserva los búferes. false si no hay memoria
void Shutdown();

// Dibuja los avatares ya ordenados de lejos a cerca. info[k] corresponde a inst[k].
void DrawAll(const Target &t, const Instance *inst, int n, const Mat44 &viewProj, DrawInfo *info);
void ForgetAll();               // invalida los sprites (cambio de mapa, reconexión)

// Reloj para el presupuesto (ticks del sistema). Sin reloj se rasteriza todo en cada cuadro (PC).
void SetClock(uint64_t (*now)(), uint32_t ticksPerSecond);
// Duración del último cuadro del juego (ticks entre dos llamadas del OSD superior): ajusta el presupuesto.
void FrameFeedback(uint64_t periodTicks);
void SetBudgetCap(float cap);            // v16: tope del presupuesto (calidad automática)

// Diagnóstico (se acumula; el OSD lo vuelca al registro de vez en cuando)
struct Stats {
    int calls, renders, reuses, skippedBudget, behind, offscreen, tooBig, noModel, noMemory, noCache;
    int trisIn, trisDrawn, pixels;
    uint64_t ticksRender, ticksComposite;
    int budgetK, slowFrames, frames;
};
extern Stats g_stats;

// Utilidades de píxel para el HUD y la prueba en PC
void PutPixel(const Target &t, int x, int y, uint8_t r, uint8_t g, uint8_t b);
void GetPixel(const Target &t, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b);
// Copia una imagen RGB565 (fila mayor, w x h) en (x0,y0) con recorte. Mucho más rápida que PutPixel.
void Blit565(const Target &t, const uint16_t *img, int w, int h, int x0, int y0);
// Igual, pero los píxeles 0 son transparentes (formas no rectangulares)
void Blit565Key(const Target &t, const uint16_t *img, int w, int h, int x0, int y0);
// Escalada (iw x ih -> dw x dh) con transparencia y espejo; holes = nh rectángulos {x0,y0,x1,y1} seguidos que no se
// pintan (o nullptr). v16.2: varios huecos (la silueta del entrenador: cabeza + cuerpo, no un rectángulo ancho).
void BlitScaledKey(const Target &t, const uint16_t *img, int iw, int ih, int x0, int y0, int dw, int dh, bool flipX,
                   const int *holes, int nh = 1);
// Lee una muestra de la pantalla con UNA sola lectura alineada de 32 bits si el formato lo permite (BGR8 en filas
// alineadas): en Azahar cada lectura del framebuffer pasa por el camino lento, así que leer byte a byte cuesta x3.
void GetPixelFast(const Target &t, int x, int y, uint8_t &r, uint8_t &g, uint8_t &b);
// Al abrir el menú SELECT (juego en pausa): suelta sprites, z y búferes por cuadro (DrawAll los vuelve a reservar
// solo). v16.3: las cachés de los modelos se conservan (ver Prewarm).
void ReleaseMemory();
// v16.3: construye al arrancar la caché de un modelo (con la memoria entera) para no tener que rehacerla nunca.
bool Prewarm(const Hero::Model &M);
uint32_t CacheBytes();                 // memoria de las cachés de modelos

} // namespace R3D
