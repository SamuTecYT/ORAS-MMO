#pragma once
// Nombre (ASCII, espanol) de la especie 1..721; "?" si no existe
namespace Species {
const char *Name(unsigned id);
const char *Local(unsigned id);   // v17: el oficial del juego en el idioma del MMO (GameText); si no está, Name()
const char *Osd(unsigned id);     // v17: para el OSD (letra latina): en japonés/coreano, el inglés
}
