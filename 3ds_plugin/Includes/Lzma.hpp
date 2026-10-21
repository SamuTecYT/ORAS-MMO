/*
 * Lzma.hpp — ORAS MMO v16.3
 * Descompresor LZMA (el de 7-Zip) para los bloques de mmo_pokes_3d.bin ("MMO3D3"): mismos píxeles que antes, el
 * archivo pesa ~28 % menos que con LZ11. Flujo LZMA1 "crudo" (sin cabecera .lzma) con tamaño conocido; el diccionario
 * es el propio búfer de salida (no reserva memoria aparte). Escrito a partir de la especificación de dominio público
 * de Igor Pavlov (LzmaSpec.cpp). Sin dependencias: se puede probar fuera del plugin.
 */
#pragma once
#include <stdint.h>

namespace Lzma {

// Probabilidades que necesita Decode() (u16 cada una) para unos lc/lp dados
uint32_t ProbCount(int lc, int lp);
// v17.0 r2: las mismas, como constante para reservar el búfer: 1847 fijas (UNA más que las 1846 de 7-Zip: aquí la tabla
// de distancias empieza en el índice 1) + las de los literales. El búfer de Follower3D tenía 1846 + ... y cada
// Decode() escribía 2 bytes FUERA de él, encima de lo que el enlazador pusiera detrás (en la v17.0, a veces, las letras
// japonesas de GameFont: el menú en japonés salía con la letra del sistema y "?" en la "ん").
static const uint32_t BASE_PROBS = 1847;
static const uint32_t PROBS_LC_LP_3 = BASE_PROBS + (0x300u << 3);   // para lc + lp <= 3

// Descomprime src (srcLen bytes) en dst hasta llenar exactamente dstLen bytes. lc/lp/pb = parámetros del flujo
// (lc <= 8, lp <= 4, pb <= 4). probs = búfer de ProbCount(lc, lp) u16 (se inicializa aquí). Devuelve dstLen si todo
// fue bien, o -1 si el flujo está corrupto (nunca lee ni escribe fuera de los búferes).
int32_t Decode(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t dstLen, int lc, int lp, int pb, uint16_t *probs);

} // namespace Lzma
