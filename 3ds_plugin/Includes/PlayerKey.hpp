/*
 * PlayerKey.hpp — v17.0 r4: llave secreta del ranking (mmo_key.txt)
 *
 * Antes el ranking reconocía a cada jugador solo por su nombre: cualquiera que se pusiera tu nombre podía cambiar o
 * borrar tus puntos. Ahora cada plugin crea UNA vez una llave al azar (16 bytes, en mmo_key.txt junto al plugin) y al
 * conectarse manda al servidor (paquete KEY, que el servidor no reenvía a nadie) una huella SHA-256 de esa llave,
 * distinta para cada servidor y sala. El primero que guarda puntos con un nombre se queda con él.
 * Para jugar con el mismo nombre en otro aparato, se copia mmo_key.txt allí. Si se borra, el nombre se libera
 * cuando lleva 30 días sin jugarse (ORAS_RANK_CLAIM_DAYS del servidor).
 */
#pragma once
#include <3ds.h>
#include <string>

namespace PlayerKey {
    void Init();                 // al arrancar: lee mmo_key.txt o crea una llave nueva y la guarda
    bool Ready();                // hay llave y está guardada en la SD (sin ella no se manda nada)
    // Huella de la llave para ese servidor y sala (lo único que sale del aparato)
    void Token(const std::string &server, int port, const std::string &room, u8 out[16]);
}
