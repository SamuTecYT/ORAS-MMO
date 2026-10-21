/*
 * GameHook.hpp — ORAS MMO v16 (fase 2: siempre activo desde v16.1; la comprobación de build protege)
 * Funciones que usan el propio motor del juego, siguiendo el protocolo seguro del KB §12.8:
 *   - al arrancar se comprueba que el juego es exactamente la build estudiada (KB §1); si no, no se toca nada;
 *   - UN solo gancho (GameSystem_Update 0x108A48), instalado una vez al arrancar y nunca quitado (§9.10);
 *   - todo lo que llama al juego ocurre en ese gancho (hilo del juego) y solo con el jugador caminando libre:
 *     Fieldmap presente, DllField cargado y SIN evento activo (§8.2). Los otros hilos solo dejan órdenes;
 *   - antes de cualquier evento (menú, guardar, puerta, combate) se borran los modelos añadidos (§8.5).
 * Funciones: amigos como personajes 3D REALES del juego (§8.3/8.4). v16.3: el teletransporte "Ir con mi amigo" (§8.6)
 * se quitó (descolocaba partes del juego).
 */
#pragma once
#include <3ds.h>
#include "Players.hpp"

namespace GameHook {

// Al arrancar: comprueba la build e instala el gancho. Siempre (v16.1 quitó la opción "Funciones nativas").
bool Install();
bool Active();                       // gancho instalado y build correcta
const char *Status();                // texto corto para el menú ("Activas", "Juego distinto", ...)

// Amigos nativos: el OSD pasa lo que ve cada cuadro; el gancho crea, mueve y borra los modelos
void FeedViews(const MMO::RemoteView *views, int n, u64 nowMs);
bool IsNative(u8 playerId);          // ese amigo se dibuja con un modelo real del juego (no con el motor propio)
void OnEmote(u8 playerId, u8 emote); // emote recibido (1-based): "Sorpresa" -> "!" del propio juego

} // namespace GameHook
