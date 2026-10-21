/*
 * ServerConfig.example.hpp — plantilla del servidor por defecto de TU compilación (ORAS MMO)
 *
 * Cómo usarla (es / en):
 *   1. Copia este archivo como "ServerConfig.local.hpp" en esta misma carpeta (Includes/).
 *      Copy this file to "ServerConfig.local.hpp" in this same folder (Includes/).
 *   2. Cambia la IP (y si quieres el puerto y la sala) por los de tu servidor.
 *      Put your server's IP (and optionally port and room) below.
 *   3. Compila. Tus jugadores se conectarán solos, sin escribir nada.
 *      Build. Your players will connect automatically.
 *
 * "ServerConfig.local.hpp" está en .gitignore: tu IP nunca llega al repositorio.
 * "ServerConfig.local.hpp" is git-ignored: your IP never reaches the repository.
 * Sin ese archivo, el plugin usa 127.0.0.1:9000 (un servidor en el mismo PC) y la sala "1234".
 * Without it, the plugin uses 127.0.0.1:9000 (a server on the same PC) and room "1234".
 * Cada jugador puede cambiarlo con mmo_server.txt ("IP" o "IP:puerto") o en SELECT -> Conexión.
 */
#pragma once

#define MMO_DEFAULT_SERVER   "203.0.113.10"   // IPv4 de tu servidor (solo números: el plugin no usa DNS)
#define MMO_DEFAULT_PORT     9000             // el mismo que PORT en server.py
#define MMO_DEFAULT_ROOM     "1234"           // sala por defecto (mejor una propia y difícil de adivinar)
