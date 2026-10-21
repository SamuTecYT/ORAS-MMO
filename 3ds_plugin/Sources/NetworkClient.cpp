#include "NetworkClient.hpp"
#include "csvc.h"
#include <CTRPluginFramework.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <malloc.h>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;

static constexpr u32 k_socSize = 0x20000;
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x0004
#endif

NetworkClient::~NetworkClient() {
    Disconnect();
    ShutdownSoc();
}

bool NetworkClient::EnsureSoc() {
    if (m_socInit) return true;
    Result r;
    // 1. Citra / Azahar (PC y Android): el emulador acepta un búfer nulo.
    if (System::IsCitra()) {
        r = socInit((u32 *)0, k_socSize);
        if (R_SUCCEEDED(r)) { m_socInit = true; return true; }
    }
    // 2. Consola real con Luma3DS: memoria de la región SYSTEM.
    u32 out = 0;
    r = svcControlMemoryUnsafe(&out, 0, k_socSize, (MemOp)(MEMOP_REGION_SYSTEM | MEMOP_ALLOC),
                               (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if (R_SUCCEEDED(r) && out) {
        r = socInit((u32 *)out, k_socSize);
        if (R_SUCCEEDED(r)) { m_socInit = true; m_hwAlloc = true; m_hwAddr = out; return true; }
        svcControlMemoryUnsafe(nullptr, out, k_socSize, MEMOP_FREE, (MemPerm)0);
    }
    // 3. Forks del emulador
    r = socInit((u32 *)0, k_socSize);
    if (R_SUCCEEDED(r)) { m_socInit = true; return true; }
    u32 *tmp = (u32 *)memalign(0x1000, k_socSize);
    if (tmp) {
        r = socInit(tmp, k_socSize);
        if (R_SUCCEEDED(r)) { m_socInit = true; return true; }
        free(tmp);
    }
    return false;
}

void NetworkClient::ShutdownSoc() {
    if (!m_socInit) return;
    socExit();
    m_socInit = false;
    if (m_hwAlloc && m_hwAddr) {
        svcControlMemoryUnsafe(nullptr, m_hwAddr, k_socSize, MEMOP_FREE, (MemPerm)0);
        m_hwAlloc = false;
        m_hwAddr = 0;
    }
}

void NetworkClient::Disconnect() {
    if (m_sock >= 0) closesocket(m_sock);
    m_sock = -1;
    m_rxLen = 0;
    m_txLen = 0;
}

int NetworkClient::Join(const std::string &ip, uint16_t port, const std::string &room, std::string &err) {
    Disconnect();
    if (!EnsureSoc()) { err = "socInit fallo"; return 0; }

    m_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sock < 0) {
        char t[32]; snprintf(t, sizeof(t), "socket err %d", errno); err = t;
        m_sock = -1;
        return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    // connect con límite de 4 s (no bloqueante + poll). Si el emulador no soporta ese modo,
    // se vuelve para siempre al connect bloqueante clásico (que siempre funcionó).
    static bool s_nonBlockBroken = false;
    int fl = s_nonBlockBroken ? -1 : fcntl(m_sock, F_GETFL, 0);
    bool nb = fl >= 0 && fcntl(m_sock, F_SETFL, fl | O_NONBLOCK) == 0;
    int rc = ::connect(m_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        int e = errno;
        if (nb && (e == EINPROGRESS || e == EALREADY || e == EWOULDBLOCK || e == EAGAIN)) {
            struct pollfd pfd;
            pfd.fd = m_sock; pfd.events = POLLOUT; pfd.revents = 0;
            int pr = poll(&pfd, 1, 4000);
            if (pr == 0) { err = "sin respuesta del servidor"; Disconnect(); return 0; }
            if (pr < 0) {                                   // poll no soportado: usar el modo clásico
                s_nonBlockBroken = true;
                err = "reintentando";
                Disconnect();
                return 0;
            }
            int soerr = 0;
            socklen_t sl = sizeof(soerr);
            if (getsockopt(m_sock, SOL_SOCKET, SO_ERROR, &soerr, &sl) == 0 && soerr != 0) {
                char t[32]; snprintf(t, sizeof(t), "sin servidor (%d)", soerr); err = t;
                Disconnect();
                return 0;
            }
            if (pfd.revents & (POLLERR | POLLHUP)) { err = "sin servidor"; Disconnect(); return 0; }
        } else {
            char t[32]; snprintf(t, sizeof(t), "sin servidor (%d)", e); err = t;
            Disconnect();
            return 0;
        }
    }
    if (nb) fcntl(m_sock, F_SETFL, fl);              // volver a bloqueante (los envíos usan MSG_DONTWAIT)
    // Envío sin bloqueo: en Azahar send() corre en el hilo del emulador y uno bloqueante
    // congela todo el juego mientras espera (KB §12.6). Reintenta hasta 2 s si el búfer está lleno.
    {
        size_t sent = 0;
        u64 sendDeadline = osGetTime() + 2000;
        while (sent < room.size()) {
            int n = send(m_sock, room.c_str() + sent, (int)(room.size() - sent), MSG_DONTWAIT);
            if (n > 0) { sent += (size_t)n; continue; }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && osGetTime() < sendDeadline) {
                svcSleepThread(5 * 1000000LL);
                continue;
            }
            err = "envio sala fallo";
            Disconnect();
            return 0;
        }
    }

    // Leer la respuesta hasta '\n' (máx. 8 s). El resto queda en m_rx.
    u64 deadline = osGetTime() + 8000;
    while (true) {
        if (!FillRx()) { err = "servidor cerro"; Disconnect(); return 0; }
        void *nl = memchr(m_rx, '\n', m_rxLen);
        if (nl) {
            size_t lineLen = (uint8_t *)nl - m_rx;
            std::string line((const char *)m_rx, lineLen);
            size_t rest = m_rxLen - lineLen - 1;
            memmove(m_rx, m_rx + lineLen + 1, rest);
            m_rxLen = rest;
            if (line.compare(0, 8, "OK:ROOM:") != 0) {
                err = (line.find("ROOM_FULL") != std::string::npos) ? "sala llena" : "rechazado";
                Disconnect();
                return 0;
            }
            size_t c = line.find_last_of(':');
            int id = atoi(line.c_str() + c + 1);
            if (id < 1 || id > MAX_MMO_PLAYERS) { err = "id invalido"; Disconnect(); return 0; }
            return id;
        }
        if (m_rxLen >= sizeof(m_rx) || osGetTime() > deadline) { err = "sin respuesta"; Disconnect(); return 0; }
        svcSleepThread(10 * 1000000LL);
    }
}

bool NetworkClient::FillRx() {
    if (m_sock < 0) return false;
    while (m_rxLen < sizeof(m_rx)) {
        int n = recv(m_sock, m_rx + m_rxLen, (int)(sizeof(m_rx) - m_rxLen), MSG_DONTWAIT);
        if (n > 0) { m_rxLen += n; continue; }
        if (n == 0) return false;                       // cierre ordenado
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
    return true;
}

bool NetworkClient::LooksValid(const uint8_t *p) {
    uint8_t t = p[0], id = p[1];
    bool typeOk = (t == MMO_PKT_POSITION || t == MMO_PKT_EMOTE || t == MMO_PKT_CHAT || t == MMO_PKT_EVENT_REQ ||
                   t == MMO_PKT_EVENT_RESP || t == MMO_PKT_EVENT_END || t == MMO_PKT_LEAVE || t == MMO_PKT_PING ||
                   t == MMO_PKT_STATS || t == MMO_PKT_BATTLE ||
                   t == 0x04 || t == 0x30);
    if (t == MMO_PKT_RANK_SNAPSHOT) return id <= MAX_MMO_PLAYERS && p[26] >= 16;   // v16: id 0 = desconectado
    return typeOk && id >= 1 && id <= MAX_MMO_PLAYERS;
}

int NetworkClient::Poll(PlayerPacket &out) {
    if (m_sock < 0) return -1;
    if (m_rxLen < MMO_PACKET_SIZE && !FillRx()) { Disconnect(); return -1; }
    // Resincronización: descartar bytes hasta que el bloque parezca un paquete.
    size_t skip = 0;
    while (m_rxLen - skip >= MMO_PACKET_SIZE && !LooksValid(m_rx + skip) && skip < MMO_PACKET_SIZE) skip++;
    if (skip) {
        memmove(m_rx, m_rx + skip, m_rxLen - skip);
        m_rxLen -= skip;
    }
    if (m_rxLen < MMO_PACKET_SIZE) return 0;
    memcpy(&out, m_rx, MMO_PACKET_SIZE);
    m_rxLen -= MMO_PACKET_SIZE;
    if (m_rxLen) memmove(m_rx, m_rx + MMO_PACKET_SIZE, m_rxLen);
    return LooksValid((const uint8_t *)&out) ? 1 : 0;
}

bool NetworkClient::Queue(const PlayerPacket &pkt) {
    if (m_sock < 0) return false;
    if (m_txLen + MMO_PACKET_SIZE > sizeof(m_tx)) {
        // 64 paquetes sin poder enviar: el servidor no responde -> reconectar
        Disconnect();
        return false;
    }
    memcpy(m_tx + m_txLen, &pkt, MMO_PACKET_SIZE);
    m_txLen += MMO_PACKET_SIZE;
    return true;
}

bool NetworkClient::Flush() {
    if (m_sock < 0) return false;
    while (m_txLen > 0) {
        int n = send(m_sock, m_tx, (int)m_txLen, MSG_DONTWAIT);
        if (n > 0) {
            m_txLen -= n;
            if (m_txLen) memmove(m_tx, m_tx + n, m_txLen);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        Disconnect();
        return false;
    }
    return true;
}
