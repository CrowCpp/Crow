#include <cstdint>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

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
constexpr int PORT = 41815;

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

bool send_masked_frame(int fd, std::uint8_t opcode, const std::string& payload)
{
    constexpr std::uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    frame.push_back(static_cast<char>(0x80 | payload.size()));
    frame.append(reinterpret_cast<const char*>(mask), sizeof(mask));
    for (std::size_t i = 0; i < payload.size(); ++i)
        frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    return ::send(fd, frame.data(), frame.size(), 0) ==
           static_cast<ssize_t>(frame.size());
}

// Read every byte the server sends until it stops, then collect each Close
// frame's payload so the whole close sequence can be inspected.
std::vector<std::string> read_close_payloads(int fd)
{
    std::string data;
    char buffer[512];
    for (;;)
    {
        const ssize_t count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0)
            break;
        data.append(buffer, static_cast<std::size_t>(count));
    }

    std::vector<std::string> close_payloads;
    std::size_t pos = 0;
    while (data.size() - pos >= 2)
    {
        const std::uint8_t first = static_cast<std::uint8_t>(data[pos]);
        const std::uint8_t second = static_cast<std::uint8_t>(data[pos + 1]);
        const std::uint8_t opcode = first & 0x0f;
        std::uint64_t length = second & 0x7f;
        std::size_t header = 2;
        if (length == 126)
        {
            if (data.size() - pos < 4)
                break;
            std::uint16_t extended = 0;
            std::memcpy(&extended, data.data() + pos + 2, sizeof(extended));
            length = ntohs(extended);
            header = 4;
        }
        if ((second & 0x80) != 0)
            header += 4;
        if (data.size() - pos < header + length)
            break;
        if (opcode == 0x8)
            close_payloads.emplace_back(data, pos + header, static_cast<std::size_t>(length));
        pos += header + static_cast<std::size_t>(length);
    }
    return close_payloads;
}
} // namespace

// A client that closes with an empty Close frame has no status code, so the
// server answers with NoStatusCodePresent (1005). RFC 6455 Section 7.4.1
// forbids 1005/1006/1015 from appearing in a Close frame, so the reply must be
// a single bodyless Close frame, never a second frame carrying the code.
TEST_CASE("test_ws_close_reserved_code_not_sent_e2e")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onmessage([](crow::websocket::connection&, const std::string&, bool) {});

    app.loglevel(crow::LogLevel::Critical);
    auto future = app.bindaddr("127.0.0.1").port(PORT).run_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    const int fd = connect_websocket();
    CHECK(fd >= 0);
    const bool sent = send_masked_frame(fd, 0x8, std::string());
    CHECK(sent);

    const std::vector<std::string> close_payloads = read_close_payloads(fd);
    ::close(fd);

    CHECK(close_payloads.size() == 1);
    for (const auto& payload : close_payloads)
        CHECK(payload.empty());

    app.stop();
    future.wait();
}
