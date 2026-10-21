/*
 * Log.hpp — ORAS MMO v13
 * Registro de diagnóstico en mmo_log.txt (junto al plugin). Solo escribe; se trunca al iniciar
 * y deja de escribir al pasar ~64 KB para no llenar la SD. Útil para depurar con los amigos.
 */
#pragma once

namespace Log {
    void Init();
    void Write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}
