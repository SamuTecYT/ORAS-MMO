/*
 * HeroAssets.hpp — ORAS MMO v13
 *
 * Carga en tiempo de ejecución los modelos 3D REALES del protagonista (Aura/May y Bruno/Brendan)
 * y sus animaciones reales (quieto, caminar, correr) desde el RomFS del propio juego del jugador.
 * El plugin no contiene ni distribuye datos de Nintendo.
 *
 * Origen (Omega Ruby / Alpha Sapphire): GARC /a/0/2/1, entrada 171 = May, 172 = Brendan.
 * Cada entrada (LZ11) es un pack "MM": sub 0 = BCH (modelo), sub 1 = pack de movimientos GF.
 *
 * Este módulo es independiente de CTRPF salvo por la fuente de bytes: en la consola/emulador se
 * usa GameRomFS; en las pruebas de PC (zig c++) se lee directamente el .cxi.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "Math3D.hpp"

namespace Hero {

// v16: también las dos bicis (GARC /a/0/2/1: 537 = Mach "SUBOBJ_CYCLE_MACH", 536 = Acro "SUBOBJ_CYCLE_DIRT"),
// cada una con su esqueleto (ruedas, bielas, pedales) y sus movimientos en los MISMOS huecos que el jugador:
// Mach 7 (quieto) / 9 (pedalear, 16 cuadros), Acro 11 (quieto) / 13 (pedalear, 16 cuadros).
enum Which { MODEL_MAY = 0, MODEL_BRENDAN = 1, MODEL_MACH = 2, MODEL_ACRO = 3, MODEL_COUNT = 4 };
enum AnimId { ANIM_IDLE = 0, ANIM_WALK = 1, ANIM_RUN = 2,
              ANIM_MACH_IDLE = 3, ANIM_MACH_RIDE = 4, ANIM_ACRO_IDLE = 5, ANIM_ACRO_RIDE = 6, ANIM_COUNT = 7 };

static const int MAX_BONES = 64;
static const int MAX_RANGES = 24;
static const int MAX_TEX = 6;

struct Texture {
    uint16_t  w, h;
    uint16_t *px;           // RGBA5551 (R bits 15-11, G 10-6, B 5-1, A bit 0), lineal, fila 0 = arriba (v = 1)
    uint8_t  *raw;          // datos ORIGINALES tal cual los tiene el juego (para encontrar y recolorear su copia)
    uint32_t  rawSize;
    uint8_t   fmt;          // formato PICA (12 = ETC1, 13 = ETC1A4, 0..9 sin comprimir)
};

struct Vertex {
    float   px, py, pz;     // espacio del modelo (pose de enlace), Y arriba, el personaje mira a +Z
    float   nx, ny, nz;     // normal unitaria
    float   u, v;           // uv0 (repetir: puede salir de [0,1])
    uint8_t bone[4];        // índices del ESQUELETO (ya traducidos desde la paleta del submesh)
    uint8_t weight[4];      // pesos que suman exactamente 255
};

struct DrawRange {
    uint32_t firstIndex;    // en Model::indices
    uint32_t indexCount;    // lista de triángulos (múltiplo de 3), CCW = cara frontal
    uint8_t  texture;       // índice en Model::tex (textura albedo de la unidad 1)
    uint8_t  alphaTest;     // 1 si la textura tiene alfa de 1 bit que recorta píxeles
    uint8_t  isFace;        // 1 si es la malla de la cara (parpadeo por desplazamiento de UV)
    uint8_t  isHead;        // v16.5: textura de la cabeza (gorro de Bruno, lazo de Aura): color propio
};

struct Model {
    bool      ok;
    bool      male;         // v16.5: Bruno (su gorro es blanco: el color del gorro tiñe también el blanco)
    int       boneCount;
    int16_t   parent[MAX_BONES];
    char      boneName[MAX_BONES][24];
    Mat34     invBind[MAX_BONES];

    Vertex   *verts;        int vertCount;      // solo mallas visibles por defecto (sin sw_parts01/03/04/05)
    uint16_t *indices;      int indexCount;
    DrawRange ranges[MAX_RANGES]; int rangeCount;
    Texture   tex[MAX_TEX]; int texCount;

    // Animaciones horneadas a 30 fps: matriz de PIEL por hueso y cuadro = World(pose) * InvBind,
    // lista para el skinning:  p' = sum_k w_k * skin[bone_k] * p
    int       animFrames[ANIM_COUNT];          // idle 1, walk 14, run 16 (según el juego)
    Mat34    *animSkin[ANIM_COUNT];            // [frame * boneCount + bone]
    Vec3     *animHead[ANIM_COUNT];            // [frame] posición (espacio modelo) de la parte superior de la cabeza
    float     animStride[ANIM_COUNT];          // unidades de mundo que avanza un ciclo completo (walk/run), 0 en idle
    float     height;                          // altura total del modelo (~33.5)
};

// Fuente de bytes del RomFS (lectura aleatoria). Devuelve false si falla.
struct ByteSource {
    virtual ~ByteSource() {}
    virtual bool Find(const char *romfsPath, uint64_t &offset, uint64_t &size) = 0;
    virtual bool Read(uint64_t offset, void *dst, uint32_t size) = 0;
};

// Carga ambos modelos con sus animaciones. Pensado para un hilo de fondo (tarda < 2 s).
// errOut recibe un mensaje corto en español si algo falla. Seguro ante datos corruptos (nunca crashea).
bool LoadAll(ByteSource &src, char *errOut, size_t errCap);
// Lee el subarchivo 0 de la entrada index del GARC (g = offset, gsize = tamaño). malloc() descomprimido o nullptr.
uint8_t *GarcEntry(ByteSource &src, uint64_t g, uint64_t gsize, uint32_t index, uint32_t maxOut, uint32_t &outSize);
bool GarcRange(ByteSource &src, uint64_t g, uint64_t gsize, uint32_t index, uint64_t &absOff, uint32_t &len);   // v17: sin leerlo
// Descomprime LZ11 (cabecera 0x11 + tamaño) en dst (hasta cap bytes). Devuelve los bytes escritos.
uint32_t Lz11Decode(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t cap);
const Model *Get(int which);          // nullptr si no está cargado
void FreeAll();

} // namespace Hero
