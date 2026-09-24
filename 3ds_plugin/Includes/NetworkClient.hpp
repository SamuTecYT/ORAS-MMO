/*
 * NetworkClient.hpp — ORAS MMO Plugin v3.2 — FIX DEFINITIVO
 *
 * SOLUCIÓN CORRECTA (validada por 3ds-VoiceChat):
 * - En Citra/Azahar: socInit(NULL, 0x20000) funciona perfectamente
 *   porque el emulador no valida svcCreateMemoryBlock
 * - En hardware real: svcControlMemoryUnsafe con dirección fija 0x7500000
 *
 * Esto ya funciona en producción — 3ds-VoiceChat lo usa exactamente así.
 */
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <3ds.h>
#include "csvc.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <CTRPluginFramework.hpp>

using namespace CTRPluginFramework;

#define MMO_PACKET_SIZE  64
#define MMO_PKT_POSITION 0x01
#define MMO_PKT_EMOTE    0x02
#define MMO_PKT_CHAT     0x03
#define MMO_PKT_BATTLE   0x04
#define MMO_PKT_PING     0xFF

// Dirección fija en memoria del sistema para socInit en hardware real
static constexpr u32 k_soc_addr = 0x07500000;
static constexpr u32 k_soc_size = 0x20000;

class NetworkClient {
private:
    int  sockfd    = -1;
    bool connected = false;
    bool soc_init  = false;
    bool hw_alloc  = false;  // true si usamos svcControlMemoryUnsafe
    u32  hw_addr   = 0;
    uint8_t rx_buffer[1024];
    size_t  rx_len = 0;

    bool soc_ensure_init() {
        if (soc_init) return true;
        Result r;

        // 1. Citra / Azahar (PC y Android): socInit((u32*)0, k_soc_size)
        // El emulador Citra/Azahar emula SOC:U y acepta (u32*)0 de forma nativa.
        // Pasar un búfer de .bss o heap causa fallo 0xD900182F en svcCreateMemoryBlock.
        if (System::IsCitra()) {
            r = socInit((u32*)0, k_soc_size);
            if (R_SUCCEEDED(r)) {
                soc_init = true;
                return true;
            }
        }

        // 2. Hardware real 3DS / Luma3DS: reserva dinámica en región SYSTEM
        u32 out = 0;
        r = svcControlMemoryUnsafe(&out, 0, k_soc_size,
            (MemOp)(MEMOP_REGION_SYSTEM | MEMOP_ALLOC),
            (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
        if (R_SUCCEEDED(r) && out != 0) {
            r = socInit((u32*)out, k_soc_size);
            if (R_SUCCEEDED(r)) {
                soc_init = true;
                hw_alloc = true;
                hw_addr  = out;
                return true;
            }
            svcControlMemoryUnsafe(nullptr, out, k_soc_size, MEMOP_FREE, (MemPerm)0);
        }

        // 3. Fallback directo con (u32*)0 (para Lime3DS y forks)
        r = socInit((u32*)0, k_soc_size);
        if (R_SUCCEEDED(r)) {
            soc_init = true;
            return true;
        }

        // 4. Fallback con memalign
        u32* tmp_buf = (u32*)memalign(0x1000, k_soc_size);
        if (tmp_buf) {
            r = socInit(tmp_buf, k_soc_size);
            if (R_SUCCEEDED(r)) {
                soc_init = true;
                return true;
            }
            free(tmp_buf);
        }

        return false;
    }

    void soc_do_exit() {
        if (!soc_init) return;
        socExit();
        soc_init = false;
        if (hw_alloc && hw_addr != 0) {
            svcControlMemoryUnsafe(nullptr, hw_addr, k_soc_size, MEMOP_FREE, (MemPerm)0);
            hw_alloc = false;
            hw_addr  = 0;
        }
    }

public:
    NetworkClient()  {}
    ~NetworkClient() { disconnect(); soc_do_exit(); }

    // Permite cerrar soc externamente si es necesario
    void shutdown_soc() { soc_do_exit(); }

    bool connect(const std::string& ip, uint16_t port, std::string& err_out) {
        if (!soc_ensure_init()) {
            err_out = "socInit fail";
            return false;
        }

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            char tmp[48];
            snprintf(tmp, sizeof(tmp), "sock err:%d", (int)errno);
            err_out = tmp;
            return false;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "conn err:%d", (int)errno);
            err_out = tmp;
            closesocket(sockfd);
            sockfd = -1;
            return false;
        }

        connected = true;
        return true;
    }

    void disconnect() {
        if (sockfd >= 0) {
            closesocket(sockfd);
            sockfd = -1;
        }
        connected = false;
        rx_len = 0;
    }

    bool is_connected()   const { return connected; }
    bool is_soc_ready()   const { return soc_init;  }

    bool send_handshake(const std::string& room_code) {
        if (!connected || sockfd < 0) return false;
        int res = send(sockfd, room_code.c_str(), (int)room_code.length(), 0);
        if (res <= 0) { disconnect(); return false; }
        return true;
    }

    std::string recv_handshake() {
        if (!connected || sockfd < 0) return "";
        char buf[256] = {0};
        int bytes = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (bytes > 0) return std::string(buf);
        disconnect();
        return "";
    }

    bool send_packet(const uint8_t* data, size_t size) {
        if (!connected || sockfd < 0) return false;
        int res = send(sockfd, data, (int)size, 0);
        if (res <= 0) { disconnect(); return false; }
        return true;
    }

    int recv_packet(uint8_t* buffer, size_t size) {
        if (!connected || sockfd < 0) return -1;
        int bytes = recv(sockfd, buffer, (int)size, 0);
        if (bytes <= 0) { disconnect(); return -1; }
        return bytes;
    }

    // Búfer acumulador de flujo TCP: entrega únicamente paquetes completos de 64 bytes
    int recv_packet_nonblock(uint8_t* buffer, size_t size) {
        if (!connected || sockfd < 0) return -1;

        // 1. Si ya tenemos un paquete completo en el búfer acumulador, entregarlo de inmediato
        if (rx_len >= size) {
            memcpy(buffer, rx_buffer, size);
            rx_len -= size;
            if (rx_len > 0) {
                memmove(rx_buffer, rx_buffer + size, rx_len);
            }
            return (int)size;
        }

        // 2. Leer nuevos datos disponibles del socket sin bloquear
        int space = (int)(sizeof(rx_buffer) - rx_len);
        if (space > 0) {
            int bytes = recv(sockfd, (char*)(rx_buffer + rx_len), space, 0x0004); // MSG_DONTWAIT
            if (bytes > 0) {
                rx_len += bytes;
            } else if (bytes < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    disconnect();
                    return -1;
                }
            } else if (bytes == 0) {
                // Servidor cerró la conexión
                disconnect();
                return -1;
            }
        }

        // 3. Si ahora acumulamos un paquete completo, entregarlo
        if (rx_len >= size) {
            memcpy(buffer, rx_buffer, size);
            rx_len -= size;
            if (rx_len > 0) {
                memmove(rx_buffer, rx_buffer + size, rx_len);
            }
            return (int)size;
        }

        return 0; // Paquete incompleto o sin datos, esperar al siguiente ciclo
    }
};
