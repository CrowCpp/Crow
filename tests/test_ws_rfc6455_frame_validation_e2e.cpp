#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0
#include "crow.h"

#include "catch2/catch_all.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#ifdef _WIN32
#define close(fd) closesocket(fd)
#define ssize_t SSIZE_T
#endif

namespace
{
constexpr int PORT = 41813;
constexpr std::uint16_t PROTOCOL_ERROR = 1002;

std::atomic<int> message_count{0};
std::atomic<int> error_count{0};

int connect_websocket()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    timeval timeout{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        return -1;
    }

    const std::string request =
      "GET /ws HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
    ::send(fd, request.data(), request.size(), 0);

    std::string response;
    char buffer[1024];
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
        {
            ::close(fd);
            return -1;
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }

    if (response.find("101 Switching Protocols") == std::string::npos)
    {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool send_masked_frame(int fd, bool fin, std::uint8_t rsv, std::uint8_t opcode, const std::string& payload)
{
    constexpr std::uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string frame;
    frame.push_back(static_cast<char>((fin ? 0x80 : 0x00) | ((rsv & 0x07) << 4) | opcode));
    if (payload.size() < 126)
    {
        frame.push_back(static_cast<char>(0x80 | payload.size()));
    }
    else if (payload.size() <= 0xFFFF)
    {
        frame.push_back(static_cast<char>(0x80 | 126));
        const std::uint16_t extended = htons(static_cast<std::uint16_t>(payload.size()));
        frame.append(reinterpret_cast<const char*>(&extended), sizeof(extended));
    }
    else
    {
        return false;
    }
    frame.append(reinterpret_cast<const char*>(mask), sizeof(mask));
    for (std::size_t i = 0; i < payload.size(); ++i)
        frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));

    return ::send(fd, frame.data(), frame.size(), 0) ==
           static_cast<ssize_t>(frame.size());
}

struct ServerObservation
{
    bool pong = false;
    bool close_frame = false;
    std::uint16_t close_code = 0;
};

ServerObservation read_server_frames(int fd)
{
    ServerObservation observation;
    std::string data;
    char buffer[512];
    for (;;)
    {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        data.append(buffer, static_cast<std::size_t>(count));

        std::size_t pos = 0;
        bool incomplete = false;
        while (!incomplete)
        {
            if (data.size() - pos < 2)
            {
                incomplete = true;
                break;
            }
            const std::uint8_t first = static_cast<std::uint8_t>(data[pos]);
            const std::uint8_t second = static_cast<std::uint8_t>(data[pos + 1]);
            const std::uint8_t opcode = first & 0x0f;
            const bool masked = (second & 0x80) != 0;
            std::uint64_t length = second & 0x7f;
            std::size_t header = 2;
            if (length == 126)
            {
                if (data.size() - pos < 4)
                {
                    incomplete = true;
                    break;
                }
                std::uint16_t extended = 0;
                std::memcpy(&extended, data.data() + pos + 2, sizeof(extended));
                length = ntohs(extended);
                header = 4;
            }
            else if (length == 127)
            {
                if (data.size() - pos < 10)
                {
                    incomplete = true;
                    break;
                }
                length = 0;
                for (std::size_t i = 0; i < 8; ++i)
                    length = (length << 8) | static_cast<std::uint8_t>(data[pos + 2 + i]);
                header = 10;
            }
            if (masked)
                header += 4;
            if (data.size() - pos < header + length)
            {
                incomplete = true;
                break;
            }
            const std::size_t payload_pos = pos + header;
            if (opcode == 0xA)
                observation.pong = true;
            if (opcode == 0x8)
            {
                observation.close_frame = true;
                if (length >= 2)
                {
                    std::uint16_t code = 0;
                    std::memcpy(&code, data.data() + payload_pos, sizeof(code));
                    observation.close_code = ntohs(code);
                }
                break;
            }
            pos = payload_pos + static_cast<std::size_t>(length);
        }
        data.erase(0, pos);
        if (observation.close_frame)
            break;
    }
    return observation;
}
} // namespace

TEST_CASE("test_ws_rfc6455_frame_validation_e2e")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onmessage([](crow::websocket::connection&, const std::string&, bool) {
          message_count++;
      })
      .onerror([](crow::websocket::connection&, const std::string&) {
          error_count++;
      });

    app.loglevel(crow::LogLevel::Critical);
    auto future = app.bindaddr("127.0.0.1").port(PORT).run_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    const int baseline_fd = connect_websocket();
    CHECK(baseline_fd >= 0);
    const bool baseline_ping_sent = send_masked_frame(baseline_fd, true, 0x0, 0x9, "hb");
    const ServerObservation baseline = read_server_frames(baseline_fd);
    CHECK(baseline_ping_sent);
    CHECK(baseline.pong);
    CHECK_FALSE(baseline.close_frame);
    ::close(baseline_fd);

    message_count.store(0);
    error_count.store(0);

    const int reserved_fd = connect_websocket();
    CHECK(reserved_fd >= 0);
    const bool reserved_sent = send_masked_frame(reserved_fd, true, 0x0, 0x3, "x");
    const ServerObservation reserved = read_server_frames(reserved_fd);
    CHECK(reserved_sent);
    CHECK(message_count.load() == 0);
    CHECK(reserved.close_frame);
    CHECK(reserved.close_code == PROTOCOL_ERROR);
    ::close(reserved_fd);

    const int fragmented_fd = connect_websocket();
    CHECK(fragmented_fd >= 0);
    const bool fragmented_sent = send_masked_frame(fragmented_fd, false, 0x0, 0x9, "x");
    const ServerObservation fragmented = read_server_frames(fragmented_fd);
    CHECK(fragmented_sent);
    CHECK_FALSE(fragmented.pong);
    CHECK(fragmented.close_frame);
    CHECK(fragmented.close_code == PROTOCOL_ERROR);
    ::close(fragmented_fd);

    const int oversized_fd = connect_websocket();
    CHECK(oversized_fd >= 0);
    const bool oversized_sent = send_masked_frame(oversized_fd, true, 0x0, 0x9, std::string(126, 'P'));
    const ServerObservation oversized = read_server_frames(oversized_fd);
    CHECK(oversized_sent);
    CHECK_FALSE(oversized.pong);
    CHECK(oversized.close_frame);
    CHECK(oversized.close_code == PROTOCOL_ERROR);
    ::close(oversized_fd);

    const int rsv_fd = connect_websocket();
    CHECK(rsv_fd >= 0);
    const bool rsv_sent = send_masked_frame(rsv_fd, true, 0x4, 0x1, "x");
    const ServerObservation rsv = read_server_frames(rsv_fd);
    CHECK(rsv_sent);
    CHECK(message_count.load() == 0);
    CHECK(rsv.close_frame);
    CHECK(rsv.close_code == PROTOCOL_ERROR);
    ::close(rsv_fd);

    app.stop();
    future.wait();
}
