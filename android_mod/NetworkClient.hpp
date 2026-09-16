#pragma once

#include <3ds.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <cerrno>

class NetworkClient {
private:
    int sockfd = -1;
    bool connected = false;

public:
    NetworkClient() {}
    
    ~NetworkClient() {
        disconnect();
    }

    bool connect(const std::string& ip, uint16_t port, int timeout_ms = 5000) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) return false;

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

        // Set non-blocking for timeout connect
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        int res = ::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (res < 0 && errno != EINPROGRESS) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(sockfd, &set);
        struct timeval timeout;
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        res = select(sockfd + 1, nullptr, &set, nullptr, &timeout);
        if (res <= 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        connected = true;
        return true;
    }

    void disconnect() {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
        connected = false;
    }

    bool is_connected() const { return connected; }

    bool send_handshake(const std::string& room_code) {
        if (!connected) return false;
        std::string msg = room_code;
        return send_data((const uint8_t*)msg.c_str(), msg.length());
    }

    std::string recv_handshake() {
        if (!connected) return "";
        char buf[256];
        memset(buf, 0, sizeof(buf));
        
        // Wait briefly for handshake response
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK); // blocking

        int bytes = recv(sockfd, buf, sizeof(buf)-1, 0);
        
        // Restore non-blocking
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        if (bytes > 0) {
            return std::string(buf);
        }
        return "";
    }

    bool send_packet(const uint8_t* data, size_t size) {
        if (!connected) return false;
        return send_data(data, size);
    }

    int recv_packet(uint8_t* buffer, size_t size) {
        if (!connected) return -1;
        int bytes = recv(sockfd, buffer, size, 0);
        if (bytes == 0) {
            disconnect();
            return -1; // disconnected
        }
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0; // no data available
        }
        return bytes;
    }

private:
    bool send_data(const uint8_t* data, size_t size) {
        size_t sent = 0;
        while (sent < size) {
            int res = send(sockfd, data + sent, size - sent, 0);
            if (res < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    usleep(10000);
                    continue;
                }
                disconnect();
                return false;
            }
            sent += res;
        }
        return true;
    }
};
