/*
 * UiGfx.hpp — ORAS MMO v16.2
 * Motor de dibujo del menú SELECT (estilo Apple: "materiales" de cristal sobre el juego desenfocado).
 *
 *  - Fondo: la imagen del juego en el momento de abrir el menú, desenfocada y oscurecida (se lee UNA vez al
 *    abrir; si no se puede, un degradado azul marino). Las formas se mezclan con lo que tienen debajo
 *    (transparencia real, esquinas suavizadas, sombras difusas) en un lienzo propio en memoria: nunca se lee
 *    la pantalla cada cuadro (en Azahar eso obliga a bajar la imagen de la GPU).
 *  - Se pinta por franjas de 48 filas (38 KB de memoria fija): la lista de formas se construye una vez por
 *    cuadro y se rasteriza en cada franja; luego se copia a la pantalla. El texto (fuente del sistema) va al final.
 *  - Muelles (Spring) con los parámetros de Apple: "response" (segundos) y amortiguación (1 = sin rebote).
 *    Siempre parten del valor actual: se pueden interrumpir y redirigir en cualquier momento.
 * Solo se usa desde el hilo del menú (juego en pausa).
 */
#pragma once
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cmath>

namespace UiGfx {

struct Rgb { u8 r, g, b; };
inline Rgb RGB(int r, int g, int b) { Rgb c = { (u8)r, (u8)g, (u8)b }; return c; }
inline Rgb Mix(Rgb a, Rgb b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return RGB((int)(a.r + (b.r - a.r) * t + 0.5f), (int)(a.g + (b.g - a.g) * t + 0.5f), (int)(a.b + (b.b - a.b) * t + 0.5f));
}

// ---- Muelle crítico (Apple, "Designing Fluid Interfaces"): rigidez (2π/response)², amortiguación 2·ζ·ω
struct Spring {
    float x = 0.f, v = 0.f, target = 0.f;
    bool  init = false;
    void Set(float t) { if (!init) { x = t; v = 0.f; init = true; } target = t; }
    void Snap(float t) { x = target = t; v = 0.f; init = true; }
    void Step(float dt, float response, float damping = 1.f);
    bool Moving() const { return init && (fabsf(target - x) > 0.02f || fabsf(v) > 0.05f); }
};

// ---- Lista de dibujo de UNA pantalla (Begin ... End)
void Begin(bool top);
int  Width();
// Formas FIJAS (paneles de cristal): entre StaticBegin y StaticEnd. Se guardan ya compuestas dentro del rectángulo
// de ChromeRect (el panel principal de la pantalla) y solo se recomponen si cambian. Llamar tras Begin.
void StaticBegin();
void StaticEnd();
void ChromeRect(int x, int y, int w, int h);
// v16.2: PÁGINA QUE ENTRA. Al cambiar de sección el contenido aparece desvaneciéndose y desplazándose (dx, dy).
// Las formas y textos entre PageBegin y PageEnd se emiten en su estado FINAL (opacidad y posición de reposo) y UiGfx
// les aplica la transición: opacidad x pg; si move, también el desplazamiento; el texto se mezcla desde textBase.
// Durante la transición la página se compone una sola vez y en cada cuadro solo se mezcla (mucho más barato).
void PageParams(float pg, int dx, int dy, Rgb textBase);   // tras Begin, una vez por pantalla
void PageBegin(bool move);
void PageEnd();
// v16.2: APARICIÓN DEL MENÚ. Mientras open < 1, todo lo de la pantalla se emite en su estado final y UiGfx lo hace
// aparecer (opacidad x open, desplazado dy; el texto se mezcla desde textBase) componiendo la pantalla una sola vez.
void OpenParams(float open, int dy, Rgb textBase);
// alpha 0..255. Las coordenadas pueden salirse de la pantalla (se recorta).
void RRect(int x, int y, int w, int h, int r, Rgb c, int alpha = 255);
void RRectV(int x, int y, int w, int h, int r, Rgb top, Rgb bottom, int alpha = 255);   // degradado vertical
void Stroke(int x, int y, int w, int h, int r, Rgb c, int alpha);                        // borde de 1 px
void Shadow(int x, int y, int w, int h, int r, int blur, int alpha);                      // sombra difusa (negra)
void Disc(float cx, float cy, float r, Rgb c, int alpha = 255);
void DiscTop(float cx, float cy, float r, Rgb c, int alpha = 255);                       // solo la mitad de arriba
void FadeLine(int x, int y, int w, Rgb c, int alpha);                                     // se desvanece en los extremos
// Texto: fuente del sistema (acentos y símbolos de botones) o la pequeña (solo ASCII, 6x10)
void Text(const char *utf8, int x, int y, Rgb c);
void TextRight(const char *utf8, int xRight, int y, Rgb c);
void TextCenter(const char *utf8, int xCenter, int y, Rgb c);
void Small(const char *utf8, int x, int y, Rgb c);
void SmallCenter(const char *utf8, int xCenter, int y, Rgb c);
int  TextW(const char *utf8);
int  SmallW(const char *utf8);
// Recorta 'in' con "..." para que quepa en maxW píxeles (fuente del sistema)
void Fit(const char *in, int maxW, char *out, size_t cap);
// Pinta todo en la pantalla: fondo + formas + after(s) (p. ej. el modelo 3D) + textos. Si ese búfer de pantalla
// ya tiene exactamente esto (misma lista de formas y textos, mismo 'extra'), no pinta nada. 'extra' = algo que
// cambia lo que dibuja 'after' (p. ej. el tiempo del modelo 3D que gira).
void End(const CTRPluginFramework::Screen &s, void (*after)(const CTRPluginFramework::Screen &) = nullptr, u32 extra = 0);
// Olvida qué hay en pantalla (después de que otra cosa haya dibujado encima, p. ej. el teclado)
void Invalidate();
// Zona que pinta el "after" de End (p. ej. el modelo 3D): se repinta cuando cambia su "extra". Antes de End.
void SetAfterRect(int x, int y, int w, int h);

// ---- Perfilado (ticks del ARM11 emulado, 268 MHz): para el log al cerrar el menú
struct Prof { u64 compose, blit, after, text, wTicks; u32 draws, skips, wHit, wMiss, chromeBuilds, pageBuilds, pageFrames; };
extern Prof g_prof;

// ---- Fondo: lee y desenfoca la imagen actual de las dos pantallas (llamar al abrir el menú, juego en pausa)
void CaptureBackground(const CTRPluginFramework::Screen &top, const CTRPluginFramework::Screen &bottom);
// Libera la memoria del fondo (al cerrar el menú)
void ReleaseBackground();

} // namespace UiGfx
