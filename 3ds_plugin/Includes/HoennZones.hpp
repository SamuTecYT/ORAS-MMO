// hoenn_zones.h -- GENERADO a partir de los datos de zonas del juego. No editar a mano.
// Fuente: RomFS de Pokemon Omega Ruby v1.0 (zonas /a/0/1/3, matrices /a/0/4/0, mapa /a/0/6/7, texto 90).
// Solo datos numericos y nombres de lugar cortos. Ver ZONES.md para el significado de cada campo.
#pragma once
#include <stdint.h>

namespace hoenn {

enum ZoneKind : uint8_t { K_OTHER=0, K_CITY, K_TOWN, K_ROUTE, K_SEA_ROUTE, K_CAVE, K_DUNGEON, K_GYM, K_POKECENTER, K_INTERIOR };
enum ZoneFlag : uint8_t { F_UNDERWATER=1, F_MART=2, F_SECRET_BASE=4, F_PLACEHOLDER=8, F_OVERWORLD=16, F_NOT_ON_MAP=32 };

// Imagen del mapa: poke_navi_map.bclim 380x270. Coordenadas en MEDIOS pixeles de esa imagen (x2).
// Pantalla inferior (layout map_lower sin desplazamiento, HIPOTESIS a confirmar con 1 captura):
//   screen_x = img_x - 30 ; screen_y = img_y - 30   (zona del mapa = 320x210 arriba)
static const int TOWNMAP_IMG_W = 380, TOWNMAP_IMG_H = 270;
static const int TOWNMAP_TO_SCREEN_DX = -30, TOWNMAP_TO_SCREEN_DY = -30;
static const int NUM_ZONES = 536;

struct LocName { uint16_t location_id; const char* es; const char* en; };
static const LocName LOC_NAMES[93] = {
    {170, "Villa Ra\xC3\xAD""z", "Littleroot Town"},
    {172, "Pueblo Escaso", "Oldale Town"},
    {174, "Pueblo Azuliza", "Dewford Town"},
    {176, "Pueblo Lavacalda", "Lavaridge Town"},
    {178, "Pueblo Pardal", "Fallarbor Town"},
    {180, "Pueblo Verdegal", "Verdanturf Town"},
    {182, "Pueblo Oromar", "Pacifidlog Town"},
    {184, "Ciudad Petalia", "Petalburg City"},
    {186, "Ciudad Portual", "Slateport City"},
    {188, "Ciudad Malvalona", "Mauville City"},
    {190, "Ciudad F\xC3\xA9""rrica", "Rustboro City"},
    {192, "Ciudad Arborada", "Fortree City"},
    {194, "Ciudad Calagua", "Lilycove City"},
    {196, "Ciudad Algaria", "Mossdeep City"},
    {198, "Arrec\xC3\xAD""polis", "Sootopolis City"},
    {200, "Ciudad Colosalia", "Ever Grande City"},
    {202, "Liga Pok\xC3\xA9""mon", "Pok\xC3\xA9""mon League"},
    {204, "Ruta 101", "Route 101"},
    {206, "Ruta 102", "Route 102"},
    {208, "Ruta 103", "Route 103"},
    {210, "Ruta 104", "Route 104"},
    {212, "Ruta 105", "Route 105"},
    {214, "Ruta 106", "Route 106"},
    {216, "Ruta 107", "Route 107"},
    {218, "Ruta 108", "Route 108"},
    {220, "Ruta 109", "Route 109"},
    {222, "Ruta 110", "Route 110"},
    {224, "Ruta 111", "Route 111"},
    {226, "Ruta 112", "Route 112"},
    {228, "Ruta 113", "Route 113"},
    {230, "Ruta 114", "Route 114"},
    {232, "Ruta 115", "Route 115"},
    {234, "Ruta 116", "Route 116"},
    {236, "Ruta 117", "Route 117"},
    {238, "Ruta 118", "Route 118"},
    {240, "Ruta 119", "Route 119"},
    {242, "Ruta 120", "Route 120"},
    {244, "Ruta 121", "Route 121"},
    {246, "Ruta 122", "Route 122"},
    {248, "Ruta 123", "Route 123"},
    {250, "Ruta 124", "Route 124"},
    {252, "Ruta 125", "Route 125"},
    {254, "Ruta 126", "Route 126"},
    {256, "Ruta 127", "Route 127"},
    {258, "Ruta 128", "Route 128"},
    {260, "Ruta 129", "Route 129"},
    {262, "Ruta 130", "Route 130"},
    {264, "Ruta 131", "Route 131"},
    {266, "Ruta 132", "Route 132"},
    {268, "Ruta 133", "Route 133"},
    {270, "Ruta 134", "Route 134"},
    {272, "Cascada Meteoro", "Meteor Falls"},
    {274, "T\xC3\xBA""nel Fervergal", "Rusturf Tunnel"},
    {276, "(?)", "???"},
    {278, "Ruinas del Desierto", "Desert Ruins"},
    {280, "Cueva Granito", "Granite Cave"},
    {282, "Bosque Petalia", "Petalburg Woods"},
    {284, "Monte Cenizo", "Mt. Chimney"},
    {286, "Desfiladero", "Jagged Pass"},
    {288, "Senda \xC3\x8D""gnea", "Fiery Path"},
    {290, "Monte P\xC3\xAD""rico", "Mt. Pyre"},
    {292, "Guarida Aqua", "Team Aqua Hideout"},
    {294, "Caverna Abisal", "Seafloor Cavern"},
    {296, "Cueva Ancestral", "Cave of Origin"},
    {298, "Calle Victoria", "Victory Road"},
    {300, "Cueva Cardumen", "Shoal Cave"},
    {302, "Malvalanova", "New Mauville"},
    {304, "Malvamar", "Sea Mauville"},
    {306, "Cueva Insular", "Island Cave"},
    {308, "Tumba Antigua", "Ancient Tomb"},
    {310, "C\xC3\xA1""mara Sellada", "Sealed Chamber"},
    {312, "Gruta Solar", "Scorched Slab"},
    {314, "Guarida Magma", "Team Magma Hideout"},
    {316, "Pilar Celeste", "Sky Pillar"},
    {318, "Resort Batalla", "Battle Resort"},
    {320, "Isla del Sur", "Southern Island"},
    {322, "S.S. Marea", "S.S. Tidal"},
    {324, "Zona Safari", "Safari Zone"},
    {326, "Bosque Espejismo", "Mirage Forest"},
    {328, "Cueva Espejismo", "Mirage Cave"},
    {330, "Isla Espejismo", "Mirage Island"},
    {332, "Monte Espejismo", "Mirage Mountain"},
    {334, "Bosque Virgen", "Trackless Forest"},
    {336, "Llanura Sinnombre", "Pathless Plain"},
    {338, "Cueva Ignota", "Nameless Cavern"},
    {340, "Cueva Incierta", "Fabled Cave"},
    {342, "Boquete Irregular", "Gnarled Den"},
    {344, "Isla Creciente", "Crescent Isle"},
    {346, "Islote Secreto", "Secret Islet"},
    {348, "Firmamento", "Soaring in the sky"},
    {350, "Costa Secreta", "Secret Shore"},
    {352, "Prado Secreto", "Secret Meadow"},
    {354, "Base Secreta", "Secret Base"},
};

// Una fila por zone_id (0..535).
//  loc      = indice en LOC_NAMES
//  tx2,ty2  = punto en el mapa (medios px de la imagen 380x270); -1 = no esta en el mapa de Hoenn
//  ow       = zona exterior de referencia (la propia si es exterior; la de la puerta si es interior); 0xFFFF = ninguna
//  ow_idx   = indice en OW_XFORM si la zona es exterior, 0xFF si no
struct ZoneInfo { uint8_t loc; uint8_t kind; uint8_t flags; uint8_t ow_idx; int16_t tx2; int16_t ty2; uint16_t ow; uint16_t matrix; };
static const ZoneInfo ZONES[536] = {
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   0 Littleroot Town
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   1 Littleroot Town
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   2 Littleroot Town
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   3 Littleroot Town
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   4 Littleroot Town
    {  0, 9,  8, 255,  167,  373, 65535,   0}, //   5 Littleroot Town
    {  0, 2, 16,   0,  167,  373,     6,   1}, //   6 Littleroot Town
    {  1, 2, 16,   1,  167,  329,     7,   1}, //   7 Oldale Town
    {  2, 2, 16,   2,  123,  439,     8,   3}, //   8 Dewford Town
    {  3, 2, 16,   3,  190,  197,     9,   7}, //   9 Lavaridge Town
    {  4, 2, 16,   4,  144,  109,    10,   8}, //  10 Fallarbor Town
    {  5, 2, 16,   5,  167,  241,    11,   6}, //  11 Verdanturf Town
    {  6, 2, 16,   6,  475,  351,    12,  20}, //  12 Pacifidlog Town
    {  7, 1, 16,   7,  101,  329,    13,   2}, //  13 Petalburg City
    {  8, 1, 16,   8,  255,  362,    14,  13}, //  14 Slateport City
    {  9, 1, 16,   9,  266,  241,    15,  14}, //  15 Mauville City
    { 10, 1, 16,  10,   79,  230,    16,   4}, //  16 Rustboro City
    { 11, 1, 16,  11,  343,  109,    17,  15}, //  17 Fortree City
    { 12, 1, 16,  12,  508,  175,    18,  16}, //  18 Lilycove City
    { 13, 1, 16,  13,  618,  210,    19,  10}, //  19 Mossdeep City
    { 14, 1, 16,  14,  553,  293,    20,  17}, //  20 Sootopolis City
    { 15, 1, 16,  15,  673,  329,    21,  18}, //  21 Ever Grande City
    { 16, 1, 16,  16,  673,  280,    22,  19}, //  22 Pok?mon League
    { 17, 3, 16,  17,  167,  351,    23,   1}, //  23 Route 101
    { 18, 3, 16,  18,  134,  329,    24,   2}, //  24 Route 102
    { 19, 3, 16,  19,  200,  307,    25,   1}, //  25 Route 103
    { 20, 3, 16,  20,   79,  274,    26,   4}, //  26 Route 104
    { 20, 3, 16,  21,   79,  318,    27,   3}, //  27 Route 104
    { 21, 4, 16,  22,   79,  373,    28,   3}, //  28 Route 105
    { 22, 4, 16,  23,  101,  417,    29,   3}, //  29 Route 106
    { 23, 4, 16,  24,  167,  439,    30,   5}, //  30 Route 107
    { 24, 4, 16,  25,  222,  439,    31,   5}, //  31 Route 108
    { 25, 4, 16,  26,  255,  417,    32,   5}, //  32 Route 109
    { 26, 3, 16,  27,  255,  296,    33,  21}, //  33 Route 110
    { 26, 3, 16,  28,  255,  296,    34,  22}, //  34 Route 110
    { 27, 3, 16,  29,  255,  127,    35,  23}, //  35 Route 111
    { 27, 3, 16,  30,  255,  171,    36,   8}, //  36 Route 111
    { 27, 3, 16,  31,  255,  208,    37,   7}, //  37 Route 111
    { 28, 3, 16,  32,  237,  197,    38,   8}, //  38 Route 112
    { 28, 3, 16,  33,  215,  197,    39,   7}, //  39 Route 112
    { 29, 3, 16,  34,  200,  109,    40,   8}, //  40 Route 113
    { 30, 3, 16,  35,  116,  131,    41,   8}, //  41 Route 114
    { 31, 3, 16,  36,   79,  175,    42,   4}, //  42 Route 115
    { 32, 3, 16,  37,  134,  219,    43,   4}, //  43 Route 116
    { 33, 3, 16,  38,  211,  241,    44,   6}, //  44 Route 117
    { 34, 3, 16,  39,  310,  241,    45,   9}, //  45 Route 118
    { 35, 3, 16,  40,  321,  131,    46,  24}, //  46 Route 119
    { 35, 3, 16,  41,  321,  197,    47,  24}, //  47 Route 119
    { 36, 3, 16,  42,  365,  142,    48,  25}, //  48 Route 120
    { 36, 3, 16,  43,  360,  160,    49,  25}, //  49 Route 120
    { 37, 3, 16,  44,  431,  175,    50,  26}, //  50 Route 121
    { 38, 4, 16,  45,  436,  208,    51,  27}, //  51 Route 122
    { 39, 3, 16,  46,  398,  241,    52,   9}, //  52 Route 123
    { 40, 4, 16,  47,  563,  186,    53,  10}, //  53 Route 124
    { 41, 4, 16,  48,  629,  170,    54,  10}, //  54 Route 125
    { 42, 4, 16,  49,  563,  274,    55,  28}, //  55 Route 126
    { 43, 4, 16,  50,  629,  274,    56,  11}, //  56 Route 127
    { 44, 4, 16,  51,  633,  329,    57,  11}, //  57 Route 128
    { 45, 4, 16,  52,  640,  351,    58,  11}, //  58 Route 129
    { 46, 4, 16,  53,  585,  351,    59,  11}, //  59 Route 130
    { 47, 4, 16,  54,  519,  351,    60,  11}, //  60 Route 131
    { 48, 4, 16,  55,  431,  351,    61,  12}, //  61 Route 132
    { 49, 4, 16,  56,  365,  351,    62,  12}, //  62 Route 133
    { 50, 4, 16,  57,  307,  351,    63,  12}, //  63 Route 134
    { 23, 4, 17,  58,  167,  439,    64,  29}, //  64 Route 107
    { 40, 4, 17,  59,  563,  186,    65,  30}, //  65 Route 124
    { 42, 4, 17,  60,  563,  274,    66,  31}, //  66 Route 126
    { 43, 4, 17,  61,  629,  274,    67, 413}, //  67 Route 127
    { 44, 4, 17,  62,  633,  329,    68, 413}, //  68 Route 128
    { 45, 4, 17,  63,  640,  351,    69, 426}, //  69 Route 129
    { 46, 4, 17,  64,  585,  351,    70, 426}, //  70 Route 130
    { 51, 5,  0, 255,   92,  153,    41,  32}, //  71 Meteor Falls
    { 51, 5,  0, 255,   92,  153,    41,  33}, //  72 Meteor Falls
    { 51, 5,  0, 255,   92,  153,    41,  34}, //  73 Meteor Falls
    { 51, 5,  0, 255,   92,  153,    41,  35}, //  74 Meteor Falls
    { 52, 5,  0, 255,  167,  217,    11,  36}, //  75 Rusturf Tunnel
    { 14, 1, 16,  65,  553,  293,    76,  37}, //  76 Sootopolis City
    { 54, 5,  0, 255,  257,  143,    35,  38}, //  77 Desert Ruins
    { 55, 5,  0, 255,   99,  439,    29,  39}, //  78 Granite Cave
    { 55, 5,  0, 255,   99,  439,    29,  40}, //  79 Granite Cave
    { 55, 5,  0, 255,   99,  439,    29,  41}, //  80 Granite Cave
    { 55, 5,  0, 255,   99,  439,    29,  42}, //  81 Granite Cave
    { 56, 0,  0, 255,   69,  279,    26,  43}, //  82 Petalburg Woods
    { 57, 0,  0, 255,  201,  151,    39,  44}, //  83 Mt. Chimney
    { 58, 0,  0, 255,  201,  174,    39,  45}, //  84 Jagged Pass
    { 59, 5,  0, 255,  223,  174,    38,  46}, //  85 Fiery Path
    { 60, 5,  0, 255,  448,  213,    51,  47}, //  86 Mt. Pyre
    { 60, 5,  0, 255,  448,  213,    51,  48}, //  87 Mt. Pyre
    { 60, 5,  0, 255,  448,  213,    51,  49}, //  88 Mt. Pyre
    { 60, 5,  0, 255,  448,  213,    51,  50}, //  89 Mt. Pyre
    { 60, 5,  0, 255,  448,  213,    51,  51}, //  90 Mt. Pyre
    { 60, 5,  0, 255,  448,  213,    51,  52}, //  91 Mt. Pyre
    { 61, 6,  0, 255,  524,  165,    18,  53}, //  92 Team Aqua Hideout
    { 61, 6,  0, 255,  524,  165,    18,  54}, //  93 Team Aqua Hideout
    { 61, 6,  0, 255,  524,  165,    18,  55}, //  94 Team Aqua Hideout
    { 61, 6,  0, 255,  524,  165,    18,  56}, //  95 Team Aqua Hideout
    { 61, 6,  0, 255,  524,  165,    18,  57}, //  96 Team Aqua Hideout
    { 61, 6,  0, 255,  524,  165,    18,  58}, //  97 Team Aqua Hideout
    { 62, 5,  0, 255,  612,  328, 65535,  59}, //  98 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  60}, //  99 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  61}, // 100 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  62}, // 101 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  63}, // 102 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  64}, // 103 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  65}, // 104 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  66}, // 105 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  67}, // 106 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  68}, // 107 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  69}, // 108 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  70}, // 109 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  71}, // 110 Seafloor Cavern
    { 62, 5,  0, 255,  612,  328, 65535,  72}, // 111 Seafloor Cavern
    { 63, 5,  0, 255,  520,  262,    20,  73}, // 112 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  74}, // 113 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  75}, // 114 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  76}, // 115 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  77}, // 116 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  78}, // 117 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  79}, // 118 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,   0}, // 119 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,   0}, // 120 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  82}, // 121 Cave of Origin
    { 63, 5,  0, 255,  520,  262,    20,  83}, // 122 Cave of Origin
    { 64, 5,  0, 255,  673,  304,    21,  84}, // 123 Victory Road
    { 64, 5,  0, 255,  673,  304,    21,  85}, // 124 Victory Road
    { 64, 5,  0, 255,  673,  304,    21,  86}, // 125 Victory Road
    { 64, 5,  0, 255,  673,  304,    22,  87}, // 126 Victory Road
    { 64, 5,  0, 255,  673,  304,    22,  88}, // 127 Victory Road
    { 65, 5,  0, 255,  645,  179,    54,  89}, // 128 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  90}, // 129 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  91}, // 130 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  92}, // 131 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  93}, // 132 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  94}, // 133 Shoal Cave
    { 65, 5,  0, 255,  645,  179,    54,  95}, // 134 Shoal Cave
    { 65, 5,  8, 255,  645,  179,    54,   0}, // 135 Shoal Cave
    { 65, 5,  8, 255,  645,  179,    54,   0}, // 136 Shoal Cave
    { 65, 5,  8, 255,  645,  179,    54,   0}, // 137 Shoal Cave
    { 65, 5,  8, 255,  645,  179,    54,   0}, // 138 Shoal Cave
    { 66, 6,  0, 255,  277,  265,    33,  96}, // 139 New Mauville
    { 66, 6,  8, 255,  277,  265,    33,   0}, // 140 New Mauville
    { 66, 6,  8, 255,  277,  265,    33,   0}, // 141 New Mauville
    { 66, 6,  8, 255,  277,  265,    33,   0}, // 142 New Mauville
    { 66, 6,  8, 255,  277,  265,    33,   0}, // 143 New Mauville
    { 66, 6,  8, 255,  277,  265,    33,   0}, // 144 New Mauville
    { 67, 6,  0, 255,  221,  416,    31,  97}, // 145 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,  98}, // 146 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,  99}, // 147 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31, 100}, // 148 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 149 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 150 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 151 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 152 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 153 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31, 106}, // 154 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31, 107}, // 155 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31, 108}, // 156 Sea Mauville
    { 67, 6,  0, 255,  221,  416,    31,   0}, // 157 Sea Mauville
    { 68, 5,  0, 255,   73,  360,    28, 109}, // 158 Island Cave
    { 69, 5,  0, 255,  358,  149,    49, 110}, // 159 Ancient Tomb
    { 50, 4, 16,  66,  274,  351,   160, 111}, // 160 Route 134
    { 50, 9,  0, 255,  274,  351,   160, 112}, // 161 Route 134
    { 70, 5,  0, 255,  274,  351,   160, 113}, // 162 Sealed Chamber
    { 70, 5,  0, 255,  274,  351,   160, 114}, // 163 Sealed Chamber
    { 71, 5,  0, 255,  364,  121,    48, 115}, // 164 Scorched Slab
    { 71, 5,  0, 255,  364,  121,    48, 116}, // 165 Scorched Slab
    { 71, 5,  0, 255,  364,  121,    48, 117}, // 166 Scorched Slab
    { 71, 5,  0, 255,  364,  121,    48, 118}, // 167 Scorched Slab
    { 71, 5,  8, 255,  364,  121,    48,   0}, // 168 Scorched Slab
    { 72, 6,  0, 255,  524,  165,    18, 119}, // 169 Team Magma Hideout
    { 72, 6,  0, 255,  524,  165,    18, 120}, // 170 Team Magma Hideout
    { 72, 6,  0, 255,  524,  165,    18, 121}, // 171 Team Magma Hideout
    { 72, 6,  0, 255,  524,  165,    18, 122}, // 172 Team Magma Hideout
    { 72, 6,  0, 255,  524,  165,    18, 123}, // 173 Team Magma Hideout
    { 72, 6,  0, 255,  524,  165,    18, 124}, // 174 Team Magma Hideout
    { 73, 5,  0, 255,  525,  328,    60, 125}, // 175 Sky Pillar
    { 73, 5,  0, 255,  525,  328,    60, 126}, // 176 Sky Pillar
    { 73, 5,  8, 255,  525,  328,    60,   0}, // 177 Sky Pillar
    { 73, 5,  8, 255,  525,  328,    60,   0}, // 178 Sky Pillar
    { 73, 5,  8, 255,  525,  328,    60,   0}, // 179 Sky Pillar
    { 73, 5,  8, 255,  525,  328,    60,   0}, // 180 Sky Pillar
    { 73, 5,  0, 255,  525,  328,    60, 127}, // 181 Sky Pillar
    { 88, 0,  0, 255,  520,  294,    55, 128}, // 182 Secret Islet
    { 90, 0,  0, 255,  650,  374,    58, 129}, // 183 Secret Shore
    { 78, 0, 32, 255,   -1,   -1, 65535, 130}, // 184 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 131}, // 185 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 132}, // 186 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 133}, // 187 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 134}, // 188 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 135}, // 189 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 136}, // 190 Mirage Forest
    { 78, 0, 32, 255,   -1,   -1, 65535, 137}, // 191 Mirage Forest
    { 79, 5, 32, 255,   -1,   -1, 65535, 138}, // 192 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 139}, // 193 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 140}, // 194 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 141}, // 195 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 142}, // 196 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 143}, // 197 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 144}, // 198 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 145}, // 199 Mirage Cave
    { 80, 0, 32, 255,   -1,   -1, 65535, 146}, // 200 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 147}, // 201 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 148}, // 202 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 149}, // 203 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 150}, // 204 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 151}, // 205 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 152}, // 206 Mirage Island
    { 80, 0, 32, 255,   -1,   -1, 65535, 153}, // 207 Mirage Island
    { 81, 0, 32, 255,   -1,   -1, 65535, 154}, // 208 Mirage Mountain
    { 74, 0, 40, 255,   -1,   -1, 65535,   0}, // 209 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 155}, // 210 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 156}, // 211 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 157}, // 212 Battle Resort
    { 74, 0, 40, 255,   -1,   -1, 65535,   0}, // 213 Battle Resort
    { 75, 0,  0, 255,  573,  397, 65535, 158}, // 214 Southern Island
    { 75, 0,  0, 255,  573,  397, 65535, 159}, // 215 Southern Island
    { 76, 6, 32, 255,   -1,   -1, 65535, 160}, // 216 S.S. Tidal
    { 76, 6, 32, 255,   -1,   -1, 65535, 161}, // 217 S.S. Tidal
    { 76, 6, 32, 255,   -1,   -1, 65535, 162}, // 218 S.S. Tidal
    { 77, 0,  0, 255,  421,  153,    50, 163}, // 219 Safari Zone
    { 77, 0,  0, 255,  421,  153,    50, 163}, // 220 Safari Zone
    { 77, 0,  0, 255,  421,  153,    50, 163}, // 221 Safari Zone
    { 77, 0,  0, 255,  421,  153,    50, 163}, // 222 Safari Zone
    {  0, 9,  0, 255,  164,  368,     6, 164}, // 223 Littleroot Town
    {  0, 9,  0, 255,  164,  368,     6, 165}, // 224 Littleroot Town
    {  0, 9,  0, 255,  171,  368,     6, 166}, // 225 Littleroot Town
    {  0, 9,  0, 255,  171,  368,     6, 167}, // 226 Littleroot Town
    {  0, 9,  0, 255,  165,  375,     6, 168}, // 227 Littleroot Town
    {  1, 8,  0, 255,  163,  335,     7, 169}, // 228 Oldale Town
    {  1, 9,  2, 255,  171,  326,     7, 170}, // 229 Oldale Town
    {  1, 9,  0, 255,  163,  327,     7, 171}, // 230 Oldale Town
    {  1, 9,  0, 255,  171,  335,     7, 172}, // 231 Oldale Town
    {  2, 8,  0, 255,  114,  438,     8, 169}, // 232 Dewford Town
    {  2, 9,  0, 255,  121,  443,     8, 173}, // 233 Dewford Town
    {  2, 9,  0, 255,  114,  432,     8, 174}, // 234 Dewford Town
    {  2, 9,  0, 255,  117,  432,     8, 175}, // 235 Dewford Town
    {  3, 8,  0, 255,  189,  194,     9, 176}, // 236 Lavaridge Town
    {  3, 9,  2, 255,  195,  193,     9, 170}, // 237 Lavaridge Town
    {  3, 9,  0, 255,  193,  203,     9, 177}, // 238 Lavaridge Town
    {  3, 9,  0, 255,  196,  203,     9, 178}, // 239 Lavaridge Town
    {  4, 8,  0, 255,  150,  107,    10, 169}, // 240 Fallarbor Town
    {  4, 9,  2, 255,  151,  114,    10, 170}, // 241 Fallarbor Town
    {  4, 9,  0, 255,  142,  107,    10, 358}, // 242 Fallarbor Town
    {  4, 9,  0, 255,  142,  107,    10, 359}, // 243 Fallarbor Town
    {  4, 9,  0, 255,  142,  114,    10, 179}, // 244 Fallarbor Town
    {  4, 9,  0, 255,  134,  105,    10, 180}, // 245 Fallarbor Town
    {  5, 8,  0, 255,  173,  236,    11, 169}, // 246 Verdanturf Town
    {  5, 9,  2, 255,  168,  236,    11, 170}, // 247 Verdanturf Town
    {  5, 9,  0, 255,  161,  240,    11, 358}, // 248 Verdanturf Town
    {  5, 9,  0, 255,  161,  240,    11, 359}, // 249 Verdanturf Town
    {  5, 9,  0, 255,  165,  247,    11, 181}, // 250 Verdanturf Town
    {  5, 9,  0, 255,  158,  246,    11, 182}, // 251 Verdanturf Town
    {  5, 9,  0, 255,  173,  247,    11, 183}, // 252 Verdanturf Town
    {  6, 8,  0, 255,  475,  345,    12, 169}, // 253 Pacifidlog Town
    {  6, 9,  0, 255,  481,  344,    12, 184}, // 254 Pacifidlog Town
    {  6, 9,  0, 255,  469,  348,    12, 185}, // 255 Pacifidlog Town
    {  6, 9,  0, 255,  477,  349,    12, 186}, // 256 Pacifidlog Town
    {  6, 9,  0, 255,  469,  344,    12, 187}, // 257 Pacifidlog Town
    {  6, 9,  0, 255,  482,  348,    12, 188}, // 258 Pacifidlog Town
    {  7, 8,  0, 255,  105,  333,    13, 169}, // 259 Petalburg City
    {  7, 9,  2, 255,  108,  331,    13, 170}, // 260 Petalburg City
    {  7, 9,  0, 255,   95,  328,    13, 189}, // 261 Petalburg City
    {  7, 9,  0, 255,   95,  328,    13, 190}, // 262 Petalburg City
    {  7, 9,  0, 255,   97,  334,    13, 191}, // 263 Petalburg City
    {  7, 9,  0, 255,  104,  336,    13, 192}, // 264 Petalburg City
    {  8, 8,  0, 255,  251,  353,    14, 169}, // 265 Slateport City
    {  8, 9,  2, 255,  254,  353,    14, 170}, // 266 Slateport City
    {  8, 9,  0, 255,  257,  367,    14, 193}, // 267 Slateport City
    {  8, 9,  0, 255,  257,  367,    14, 194}, // 268 Slateport City
    {  8, 9,  0, 255,  249,  345,    14, 358}, // 269 Slateport City
    {  8, 9,  0, 255,  249,  345,    14, 359}, // 270 Slateport City
    {  8, 9,  0, 255,  247,  349,    14, 195}, // 271 Slateport City
    {  8, 9,  0, 255,  247,  356,    14, 196}, // 272 Slateport City
    {  8, 9,  0, 255,  260,  357,    14, 197}, // 273 Slateport City
    {  8, 9,  0, 255,  260,  357,    14, 198}, // 274 Slateport City
    {  8, 9,  0, 255,  258,  345,    14, 199}, // 275 Slateport City
    {  8, 9,  0, 255,  254,  372,    14, 200}, // 276 Slateport City
    {  8, 9,  0, 255,  249,  361,    14, 201}, // 277 Slateport City
    {  9, 8,  0, 255,  258,  234,    15, 169}, // 278 Mauville City
    {  9, 9,  2, 255,  273,  234,    15, 170}, // 279 Mauville City
    {  9, 9,  0, 255,  278,  241,   445, 202}, // 280 Mauville City
    {  9, 9,  0, 255,  258,  245,   445, 203}, // 281 Mauville City
    {  9, 9,  0, 255,  263,  236,   446, 204}, // 282 Mauville City
    {  9, 9,  0, 255,  266,  236,   446, 205}, // 283 Mauville City
    { 10, 8,  0, 255,   81,  235,    16, 169}, // 284 Rustboro City
    { 10, 9,  2, 255,   81,  242,    16, 170}, // 285 Rustboro City
    { 10, 9,  0, 255,   80,  216,    16, 206}, // 286 Rustboro City
    { 10, 9,  0, 255,   80,  216,    16, 207}, // 287 Rustboro City
    { 10, 9,  0, 255,   80,  216,    16, 208}, // 288 Rustboro City
    { 10, 9,  0, 255,   85,  232,    16, 209}, // 289 Rustboro City
    { 10, 9,  0, 255,   87,  242,    16, 210}, // 290 Rustboro City
    { 10, 9,  0, 255,   87,  242,    16, 211}, // 291 Rustboro City
    { 10, 9,  0, 255,   78,  243,    16, 212}, // 292 Rustboro City
    { 10, 9,  0, 255,   78,  243,    16, 213}, // 293 Rustboro City
    { 10, 9,  0, 255,   85,  242,    16, 214}, // 294 Rustboro City
    { 10, 9,  0, 255,   88,  232,    16, 215}, // 295 Rustboro City
    { 10, 9,  0, 255,   88,  221,    16, 216}, // 296 Rustboro City
    { 10, 9,  0, 255,   80,  228,    16, 217}, // 297 Rustboro City
    { 10, 9,  0, 255,   80,  228,    16, 218}, // 298 Rustboro City
    { 10, 9,  0, 255,   80,  228,    16, 219}, // 299 Rustboro City
    { 10, 9,  0, 255,   78,  235,    16, 220}, // 300 Rustboro City
    { 11, 8,  0, 255,  335,  105,    17, 169}, // 301 Fortree City
    { 11, 9,  2, 255,  335,  114,    17, 170}, // 302 Fortree City
    { 11, 9,  0, 255,  338,  104,    17, 221}, // 303 Fortree City
    { 11, 9,  0, 255,  341,  104,    17, 222}, // 304 Fortree City
    { 11, 9,  0, 255,  345,  104,    17, 223}, // 305 Fortree City
    { 11, 9,  0, 255,  348,  104,    17, 224}, // 306 Fortree City
    { 11, 9,  0, 255,  339,  113,    17, 225}, // 307 Fortree City
    { 11, 9,  0, 255,  351,  113,    17, 226}, // 308 Fortree City
    { 12, 8,  0, 255,  500,  170,    18, 169}, // 309 Lilycove City
    { 12, 9,  0, 255,  508,  174,    18, 227}, // 310 Lilycove City
    { 12, 9,  0, 255,  508,  174,    18, 228}, // 311 Lilycove City
    { 12, 9,  0, 255,  493,  166,    18, 229}, // 312 Lilycove City
    { 12, 9,  0, 255,  493,  166,    18, 230}, // 313 Lilycove City
    { 12, 9,  0, 255,  500,  175,    18, 358}, // 314 Lilycove City
    { 12, 9,  0, 255,  500,  175,    18, 359}, // 315 Lilycove City
    { 12, 9,  0, 255,  509,  170,    18, 231}, // 316 Lilycove City
    { 12, 9,  0, 255,  495,  180,    18, 232}, // 317 Lilycove City
    { 12, 9,  0, 255,  507,  166,    18, 233}, // 318 Lilycove City
    { 12, 9,  0, 255,  510,  166,    18, 234}, // 319 Lilycove City
    { 12, 9,  0, 255,  517,  170,    18, 235}, // 320 Lilycove City
    { 12, 9,  0, 255,  494,  173,    18, 236}, // 321 Lilycove City
    { 12, 9,  0, 255,  495,  170,    18, 237}, // 322 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 238}, // 323 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 239}, // 324 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 240}, // 325 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 241}, // 326 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 242}, // 327 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18, 243}, // 328 Lilycove City
    { 12, 9,  0, 255,  501,  166,    18,   0}, // 329 Lilycove City
    { 13, 8,  0, 255,  617,  210,    19, 169}, // 330 Mossdeep City
    { 13, 9,  2, 255,  621,  210,    19, 170}, // 331 Mossdeep City
    { 13, 9,  0, 255,  616,  206,    19, 244}, // 332 Mossdeep City
    { 13, 9,  0, 255,  637,  212,    19, 245}, // 333 Mossdeep City
    { 13, 9,  0, 255,  626,  207,    19, 246}, // 334 Mossdeep City
    { 13, 9,  0, 255,  610,  207,    19, 247}, // 335 Mossdeep City
    { 13, 9,  0, 255,  613,  210,    19, 248}, // 336 Mossdeep City
    { 13, 9,  0, 255,  635,  208,    19, 249}, // 337 Mossdeep City
    { 13, 9,  0, 255,  635,  208,    19, 250}, // 338 Mossdeep City
    { 13, 9,  0, 255,  622,  213,    19, 251}, // 339 Mossdeep City
    { 13, 9,  0, 255,  622,  213,    19,   0}, // 340 Mossdeep City
    { 14, 8,  0, 255,  559,  293,    20, 169}, // 341 Sootopolis City
    { 14, 9,  2, 255,  548,  293,    20, 170}, // 342 Sootopolis City
    { 14, 9,  0, 255,  547,  285,    20, 253}, // 343 Sootopolis City
    { 14, 9,  0, 255,  558,  286,    20, 254}, // 344 Sootopolis City
    { 14, 9,  0, 255,  547,  288,    20, 255}, // 345 Sootopolis City
    { 14, 9,  0, 255,  559,  289,    20, 256}, // 346 Sootopolis City
    { 14, 9,  0, 255,  547,  291,    20, 257}, // 347 Sootopolis City
    { 14, 9,  0, 255,  562,  293,    20, 258}, // 348 Sootopolis City
    { 14, 9,  0, 255,  546,  295,    20, 259}, // 349 Sootopolis City
    { 14, 9,  0, 255,  561,  292,    20, 260}, // 350 Sootopolis City
    { 14, 9,  8, 255,  547,  285,    20,   0}, // 351 Sootopolis City
    { 14, 9,  8, 255,  547,  285,    20,   0}, // 352 Sootopolis City
    { 15, 8,  0, 255,  676,  323,    21, 169}, // 353 Ever Grande City
    { 16, 6,  0, 255,  672,  269,    22, 261}, // 354 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 262}, // 355 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 263}, // 356 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 264}, // 357 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 265}, // 358 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 266}, // 359 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 267}, // 360 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 268}, // 361 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 269}, // 362 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 270}, // 363 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 271}, // 364 Pok?mon League
    { 16, 6,  0, 255,  672,  269,    22, 272}, // 365 Pok?mon League
    { 20, 9,  0, 255,   75,  268,    26, 273}, // 366 Route 104
    { 20, 9,  0, 255,   82,  316,    27, 274}, // 367 Route 104
    { 25, 9,  0, 255,  250,  393,    32, 275}, // 368 Route 109
    { 26, 9,  0, 255,  253,  321,    33, 276}, // 369 Route 110
    { 26, 9,  0, 255,  252,  268,    33, 277}, // 370 Route 110
    { 26, 9,  0, 255,  249,  306,    33, 278}, // 371 Route 110
    { 26, 9,  0, 255,  249,  306,    33, 279}, // 372 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 280}, // 373 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 281}, // 374 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 282}, // 375 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 283}, // 376 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 284}, // 377 Route 110
    { 26, 9,  0, 255,  255,  296,    33, 285}, // 378 Route 110
    { 26, 9,  0, 255,  249,  306,    33,   0}, // 379 Route 110
    { 26, 9,  0, 255,  249,  306,    33,   0}, // 380 Route 110
    { 26, 9,  0, 255,  249,  306,    33,   0}, // 381 Route 110
    { 27, 9,  0, 255,  250,  217,    37, 286}, // 382 Route 111
    { 27, 9,  0, 255,  257,  170,    36, 287}, // 383 Route 111
    { 28, 9,  0, 255,  221,  192,    39, 288}, // 384 Route 112
    { 28, 9,  0, 255,  205,  202,    39, 289}, // 385 Route 112
    { 29, 9,  0, 255,  186,  103,    40, 290}, // 386 Route 113
    { 30, 9,  0, 255,  121,  104,    41, 291}, // 387 Route 114
    { 30, 9,  0, 255,  121,  104,    41, 292}, // 388 Route 114
    { 30, 9,  0, 255,  120,  126,    41, 293}, // 389 Route 114
    { 32, 9,  0, 255,  127,  214,    43, 294}, // 390 Route 116
    { 33, 9,  0, 255,  239,  238,    44, 295}, // 391 Route 117
    { 35, 9,  0, 255,  313,  126,    46, 296}, // 392 Route 119
    { 35, 9,  0, 255,  313,  126,    46, 297}, // 393 Route 119
    { 35, 9,  0, 255,  325,  191,    47, 298}, // 394 Route 119
    { 37, 9,  0, 255,  417,  170,    50, 299}, // 395 Route 121
    { 39, 9,  0, 255,  354,  239,    52, 300}, // 396 Route 123
    { 40, 9,  0, 255,  585,  196,    53, 301}, // 397 Route 124
    { 77, 0,  8, 255,  421,  153, 65535,   0}, // 398 Safari Zone
    { 53, 0, 32, 255,   -1,   -1, 65535, 302}, // 399 ???
    { 92, 9, 36, 255,   -1,   -1, 65535, 303}, // 400 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 304}, // 401 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 305}, // 402 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 306}, // 403 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 307}, // 404 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 308}, // 405 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 309}, // 406 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 310}, // 407 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 311}, // 408 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 312}, // 409 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 313}, // 410 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 314}, // 411 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 315}, // 412 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 316}, // 413 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 317}, // 414 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 318}, // 415 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 319}, // 416 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 320}, // 417 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 321}, // 418 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 322}, // 419 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 323}, // 420 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 324}, // 421 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 325}, // 422 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 326}, // 423 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 327}, // 424 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 328}, // 425 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 329}, // 426 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 330}, // 427 Secret Base
    {  2, 7,  0, 255,  117,  443,     8, 331}, // 428 Dewford Town
    {  3, 7,  0, 255,  186,  204,     9, 332}, // 429 Lavaridge Town
    {  3, 7,  0, 255,  186,  204,     9,   0}, // 430 Lavaridge Town
    {  7, 7,  0, 255,  100,  329,    13, 333}, // 431 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 334}, // 432 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 335}, // 433 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 336}, // 434 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 337}, // 435 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 338}, // 436 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 339}, // 437 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 340}, // 438 Petalburg City
    {  7, 7,  0, 255,  100,  329,    13, 341}, // 439 Petalburg City
    {  9, 7,  0, 255,  263,  234,   445, 342}, // 440 Mauville City
    { 10, 7,  0, 255,   85,  221,    16, 343}, // 441 Rustboro City
    { 11, 7,  0, 255,  343,  110,    17, 344}, // 442 Fortree City
    { 13, 7,  0, 255,  623,  206,    19, 345}, // 443 Mossdeep City
    { 14, 7,  0, 255,  553,  295,    20, 346}, // 444 Sootopolis City
    {  9, 1, 16,  67,  266,  241,   445, 347}, // 445 Mauville City
    {  9, 1, 16,  68,  266,  241,   446, 348}, // 446 Mauville City
    {  9, 9,  0, 255,  274,  237,   446, 349}, // 447 Mauville City
    {  9, 9,  0, 255,  274,  238,   445, 350}, // 448 Mauville City
    {  9, 9,  0, 255,  274,  240,   445, 351}, // 449 Mauville City
    {  9, 9,  0, 255,  269,  234,   445, 352}, // 450 Mauville City
    {  9, 9,  0, 255,  269,  247,   445, 353}, // 451 Mauville City
    { 63, 5,  0, 255,  520,  262,    20, 354}, // 452 Cave of Origin
    { 13, 9,  0, 255,  636,  215,    19, 355}, // 453 Mossdeep City
    { 14, 9,  0, 255,  551,  290,    20, 356}, // 454 Sootopolis City
    { 14, 9,  0, 255,  551,  290,    20, 357}, // 455 Sootopolis City
    {  4, 9,  8, 255,  142,  107,    10,   0}, // 456 Fallarbor Town
    {  5, 9,  8, 255,  161,  240,    11,   0}, // 457 Verdanturf Town
    {  8, 9,  8, 255,  255,  362,    14,   0}, // 458 Slateport City
    { 12, 9,  8, 255,  508,  175,    18,   0}, // 459 Lilycove City
    { 81, 0, 32, 255,   -1,   -1, 65535, 360}, // 460 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 361}, // 461 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 362}, // 462 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 363}, // 463 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 364}, // 464 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 365}, // 465 Mirage Mountain
    { 81, 0, 32, 255,   -1,   -1, 65535, 366}, // 466 Mirage Mountain
    { 87, 0, 32, 255,   -1,   -1, 65535, 367}, // 467 Crescent Isle
    { 82, 0, 32, 255,   -1,   -1, 65535, 368}, // 468 Trackless Forest
    { 83, 5, 32, 255,   -1,   -1, 65535, 369}, // 469 Pathless Plain
    { 84, 5, 32, 255,   -1,   -1, 65535,  28}, // 470 Nameless Cavern
    { 85, 5, 32, 255,   -1,   -1, 65535, 371}, // 471 Fabled Cave
    { 86, 5, 32, 255,   -1,   -1, 65535, 372}, // 472 Gnarled Den
    { 53, 0, 40, 255,   -1,   -1, 65535,   0}, // 473 ???
    { 53, 0, 40, 255,   -1,   -1, 65535,   0}, // 474 ???
    { 91, 0,  0, 255,  562,  328,    59, 373}, // 475 Secret Meadow
    { 39, 9,  0, 255,  377,  235,    52, 374}, // 476 Route 123
    { 39, 9,  0, 255,  346,  238,    52, 427}, // 477 Route 123
    { 76, 6, 32, 255,   -1,   -1, 65535, 375}, // 478 S.S. Tidal
    { 92, 9, 36, 255,   -1,   -1, 65535, 376}, // 479 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 377}, // 480 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 378}, // 481 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 379}, // 482 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 380}, // 483 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 381}, // 484 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 382}, // 485 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 383}, // 486 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 384}, // 487 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 385}, // 488 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 386}, // 489 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 387}, // 490 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 388}, // 491 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 389}, // 492 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 390}, // 493 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 391}, // 494 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 392}, // 495 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 393}, // 496 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 394}, // 497 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 395}, // 498 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 396}, // 499 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 397}, // 500 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 398}, // 501 Secret Base
    { 92, 9, 36, 255,   -1,   -1, 65535, 399}, // 502 Secret Base
    {  9, 9,  0, 255,  284,  238,    15, 400}, // 503 Mauville City
    {  9, 9,  0, 255,  275,  240,   446, 401}, // 504 Mauville City
    {  9, 9,  0, 255,  273,  251,    15, 402}, // 505 Mauville City
    {  9, 9,  0, 255,  258,  251,    15, 403}, // 506 Mauville City
    {  9, 9,  0, 255,  256,  243,   446, 404}, // 507 Mauville City
    { 79, 5, 32, 255,   -1,   -1, 65535, 405}, // 508 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 406}, // 509 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 407}, // 510 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 408}, // 511 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 409}, // 512 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 410}, // 513 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 411}, // 514 Mirage Cave
    { 79, 5, 32, 255,   -1,   -1, 65535, 412}, // 515 Mirage Cave
    { 74, 0, 32, 255,   -1,   -1, 65535, 414}, // 516 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 415}, // 517 Battle Resort
    { 74, 8, 32, 255,   -1,   -1, 65535, 169}, // 518 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 416}, // 519 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 417}, // 520 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 418}, // 521 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 419}, // 522 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 420}, // 523 Battle Resort
    { 74, 0, 32, 255,   -1,   -1, 65535, 421}, // 524 Battle Resort
    { 12, 9,  0, 255,  501,  166,    18, 422}, // 525 Lilycove City
    { 84, 5, 32, 255,   -1,   -1, 65535, 423}, // 526 Nameless Cavern
    { 85, 5, 32, 255,   -1,   -1, 65535, 424}, // 527 Fabled Cave
    { 86, 5, 32, 255,   -1,   -1, 65535, 425}, // 528 Gnarled Den
    { 39, 9,  0, 255,  346,  238,    52, 427}, // 529 Route 123
    { 39, 9,  0, 255,  346,  238,    52, 427}, // 530 Route 123
    { 39, 9,  0, 255,  346,  238,    52, 427}, // 531 Route 123
    { 55, 5,  0, 255,   99,  439,    29, 428}, // 532 Granite Cave
    { 60, 5,  0, 255,  448,  213,    51, 429}, // 533 Mt. Pyre
    { 14, 9,  0, 255,  553,  293,    20, 430}, // 534 Sootopolis City
    { 89, 0, 32, 255,   -1,   -1, 65535, 302}, // 535 Soaring in the sky
};

// Zonas exteriores: rectangulo de la zona en unidades de mundo LOCALES a su matriz (x0,z0,x1,z1;
// 720 u = 1 chunk, 18 u = 1 casilla) -> rectangulo en el mapa (medios px). Interpolacion lineal + clamp.
struct OwXform { uint16_t zone; int16_t x0, z0, x1, z1; int16_t r0, r1, r2, r3; };
static const OwXform OW_XFORM[69] = {
    {  6,  1440,  2880,  2160,  3600,  156,  362,  178,  384}, // Littleroot Town
    {  7,  1440,  1440,  2160,  2160,  156,  318,  178,  340}, // Oldale Town
    {  8,  2880,  5040,  4320,  5760,  111,  428,  135,  450}, // Dewford Town
    {  9,  1440,  2160,  2160,  2880,  179,  184,  201,  210}, // Lavaridge Town
    { 10,  2880,  1440,  3600,  2160,  131,   98,  157,  120}, // Fallarbor Town
    { 11,  1440,  1440,  2160,  2160,  156,  229,  178,  253}, // Verdanturf Town
    { 12,     0,     0,   720,  1440,  464,  340,  486,  362}, // Pacifidlog Town
    { 13,  1440,  1440,  2160,  2880,   90,  318,  112,  340}, // Petalburg City
    { 14,     0,     0,  1440,  1440,  244,  340,  266,  384}, // Slateport City
    { 15,     0,     0,   720,   720,  244,  229,  288,  253}, // Mauville City
    { 16,  1440,  2880,  2880,  4320,   68,  208,   90,  252}, // Rustboro City
    { 17,     0,     0,  1440,   720,  332,   98,  354,  120}, // Fortree City
    { 18,     0,     0,  2160,  1440,  486,  164,  530,  186}, // Lilycove City
    { 19,  2880,  2160,  5040,  3600,  596,  199,  640,  221}, // Mossdeep City
    { 20,     0,     0,  2160,  2160,  542,  282,  564,  304}, // Sootopolis City
    { 21,     0,     0,  1440,  1440,  662,  316,  684,  342}, // Ever Grande City
    { 22,     0,     0,  1440,  2160,  662,  267,  684,  293}, // Pok?mon League
    { 23,  1440,  2160,  2160,  2880,  156,  340,  178,  362}, // Route 101
    { 24,  2160,  2160,  3600,  2880,  112,  318,  156,  340}, // Route 102
    { 25,  1440,   720,  3600,  1440,  156,  296,  244,  318}, // Route 103
    { 26,  1440,  4320,  2880,  5760,   68,  252,   90,  296}, // Route 104
    { 27,  1440,  1440,  2880,  2880,   68,  296,   90,  340}, // Route 104
    { 28,  1440,  2880,  2880,  4320,   68,  340,   90,  406}, // Route 105
    { 29,  1440,  4320,  3600,  5040,   68,  406,  134,  428}, // Route 106
    { 30,  1440,  4320,  2880,  5040,  134,  428,  200,  450}, // Route 107
    { 31,  2880,  4320,  5040,  5040,  200,  428,  244,  450}, // Route 108
    { 32,  5040,  2880,  6480,  5040,  244,  384,  266,  450}, // Route 109
    { 33,     0,     0,  1440,  2880,  244,  252,  266,  340}, // Route 110
    { 34,     0,     0,  1440,  2880,  244,  252,  266,  340}, // Route 110
    { 35,     0,     0,  1440,  2880,  244,   98,  266,  157}, // Route 111
    { 36,  6480,  1440,  7920,  2880,  244,  157,  266,  186}, // Route 111
    { 37,  3600,  2160,  5040,  4320,  244,  186,  266,  230}, // Route 111
    { 38,  5760,  2160,  6480,  2880,  229,  185,  244,  209}, // Route 112
    { 39,  2160,  1440,  3600,  2880,  200,  185,  229,  209}, // Route 112
    { 40,  3600,  1440,  6480,  2160,  156,   98,  244,  120}, // Route 113
    { 41,  1440,  1440,  2880,  4320,  101,   98,  131,  164}, // Route 114
    { 42,  1440,     0,  2880,  2880,   68,  142,   90,  208}, // Route 115
    { 43,  2880,  2880,  5040,  3600,   90,  208,  178,  230}, // Route 116
    { 44,  2160,  1440,  3600,  2160,  178,  230,  244,  252}, // Route 117
    { 45,  1440,  1440,  4320,  2160,  288,  230,  332,  252}, // Route 118
    { 46,  1440,  1440,  2880,  3600,  310,   98,  332,  164}, // Route 119
    { 47,  1440,  3600,  2880,  5760,  310,  164,  332,  230}, // Route 119
    { 48,  1440,  1440,  2880,  5040,  354,   98,  376,  186}, // Route 120
    { 49,  1440,  2880,  2160,  5040,  354,  133,  365,  186}, // Route 120
    { 50,     0,     0,  2880,   720,  376,  164,  486,  186}, // Route 121
    { 51,     0,     0,  1440,  1440,  414,  186,  458,  230}, // Route 122
    { 52,  4320,  1440,  8640,  2160,  332,  230,  464,  252}, // Route 123
    { 53,  1440,  2160,  2880,  3600,  530,  142,  596,  230}, // Route 124
    { 54,  3600,  1440,  5040,  2160,  596,  142,  662,  198}, // Route 125
    { 55,  1440,  1440,  3600,  3600,  530,  230,  596,  318}, // Route 126
    { 56,  5040,  1440,  6480,  2880,  596,  230,  662,  318}, // Route 127
    { 57,  5040,  2880,  7200,  3600,  605,  318,  661,  340}, // Route 128
    { 58,  4320,  3600,  6480,  5040,  618,  340,  662,  362}, // Route 129
    { 59,  2880,  3600,  4320,  5040,  552,  340,  618,  362}, // Route 130
    { 60,  1440,  2880,  2880,  5040,  486,  340,  552,  362}, // Route 131
    { 61,  5760,  1440,  7920,  2880,  398,  340,  464,  362}, // Route 132
    { 62,  3600,  1440,  5760,  2880,  332,  340,  398,  362}, // Route 133
    { 63,  1440,  1440,  3600,  2880,  282,  340,  332,  362}, // Route 134
    { 64,     0,     0,   720,   720,  134,  428,  200,  450}, // Route 107
    { 65,     0,     0,  1440,  1440,  530,  142,  596,  230}, // Route 124
    { 66,     0,     0,  2160,  2160,  530,  230,  596,  318}, // Route 126
    { 67,  5040,  1440,  6480,  2880,  596,  230,  662,  318}, // Route 127
    { 68,  5040,  2880,  7200,  3600,  605,  318,  661,  340}, // Route 128
    { 69,  4320,  3600,  6480,  5040,  618,  340,  662,  362}, // Route 129
    { 70,  2880,  3600,  4320,  5040,  552,  340,  618,  362}, // Route 130
    { 76,     0,     0,   720,   720,  542,  282,  564,  304}, // Sootopolis City
    {160,     0,     0,   720,   720,  266,  340,  282,  362}, // Route 134
    {445,     0,     0,  2880,  2160,  244,  229,  288,  253}, // Mauville City
    {446,     0,     0,  2880,  2160,  244,  229,  288,  253}, // Mauville City
};

// Devuelve true y el punto en la IMAGEN del mapa (px, float) para un jugador en zone_id con
// posicion local (x = float en 0x...894, z = float en 0x...89C). Para interiores devuelve el punto fijo.
inline bool ZoneToTownmap(uint16_t zone_id, float x, float z, float& px, float& py) {
    if (zone_id >= NUM_ZONES) return false;
    const ZoneInfo& zi = ZONES[zone_id];
    if (zi.ow_idx != 0xFF) {
        const OwXform& t = OW_XFORM[zi.ow_idx];
        float u = (x - t.x0) / (float)(t.x1 - t.x0), v = (z - t.z0) / (float)(t.z1 - t.z0);
        u = u < 0 ? 0 : (u > 1 ? 1 : u);
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        px = (t.r0 + u * (t.r2 - t.r0)) * 0.5f;
        py = (t.r1 + v * (t.r3 - t.r1)) * 0.5f;
        return true;
    }
    if (zi.tx2 < 0) return false;
    px = zi.tx2 * 0.5f; py = zi.ty2 * 0.5f;
    return true;
}

inline const LocName& ZoneName(uint16_t zone_id) { return LOC_NAMES[zone_id < NUM_ZONES ? ZONES[zone_id].loc : 0]; }

} // namespace hoenn
