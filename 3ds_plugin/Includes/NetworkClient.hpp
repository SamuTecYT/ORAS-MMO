/*
 * NetworkClient.hpp — ORAS MMO v13
 *
 * Cliente TCP del plugin. Cambios respecto a v12.6:
 *  - El handshake se lee hasta '\n'; lo que llegue después (paquetes de otros jugadores)
 *    se guarda en el búfer de recepción en vez de perderse (antes desalineaba el flujo para siempre).
 *  - Envío no bloqueante con búfer propio: nunca se parte un paquete aunque el socket esté lleno.
 *  - Recepción con resincronización: si llega un bloque que no parece un paquete válido se descarta
 *    byte a byte hasta recuperar el alineamiento.
 * Solo lo usa el hilo de red (no es thread-safe por diseño).
 */
#pragma once
#include <3ds.h>
#include <string>
#include "Protocol.hpp"

class NetworkClient {
public:
    NetworkClient() {}
    ~NetworkClient();

    // Conecta, envía el código de sala y lee "OK:ROOM:<sala>:<id>\n". Devuelve el id (1..12) o 0.
    int  Join(const std::string &ip, uint16_t port, const std::string &room, std::string &err);
    void Disconnect();
    bool IsConnected() const { return m_sock >= 0; }
    // v17.0 r4: el servidor respondió "OK:ROOM:<sala>:KEY:<id>": guarda el ranking con llave (PlayerKey)
    bool ServerKeys() const { return m_keys; }

    // Encola un paquete; Flush() intenta enviarlo sin bloquear. false = conexión perdida.
    bool Queue(const PlayerPacket &pkt);
    bool Flush();

    // Extrae el siguiente paquete completo. 1 = paquete, 0 = no hay, -1 = conexión perdida.
    int  Poll(PlayerPacket &out);

    void ShutdownSoc();

private:
    bool EnsureSoc();
    bool FillRx();                 // lee lo disponible sin bloquear; false = conexión perdida
    static bool LooksValid(const uint8_t *p);

    int     m_sock    = -1;
    bool    m_keys    = false;
    bool    m_socInit = false;
    bool    m_hwAlloc = false;
    u32     m_hwAddr  = 0;

    uint8_t m_rx[4096];
    size_t  m_rxLen = 0;
    uint8_t m_tx[4096];
    size_t  m_txLen = 0;
};
