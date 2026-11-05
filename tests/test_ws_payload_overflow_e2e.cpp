/**
* Crow WebSocket maximum-payload bypass through 64-bit length overflow.
*
* A continuation frame that declares a 64-bit length close to UINT64_MAX makes
* `message_.size() + remaining_length_` wrap past zero, so the sum slips under
* the configured limit and the payload cap is disabled for the connection.
*
* Build:
*   g++ -std=c++17 -I <crow>/include -I <asio>/asio/include \
*       -DASIO_STANDALONE -O1 -o ws_payload_overflow_e2e \
*       ws_payload_overflow_e2e.cpp -lpthread
*
* Loopback only. The configured maximum message payload is 16 bytes.
*/

#include <cstdint>
#include <atomic>
#include <chrono>
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
constexpr int PORT = 41811;
constexpr std::uint64_t LIMIT = 16;

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

    timeval timeout{2, 0};
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

bool send_masked_frame(int fd, bool fin, std::uint8_t opcode, const std::string& payload)
{
    if (payload.size() >= 126)
        return false;

    constexpr std::uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string frame;
    frame.reserve(2 + 4 + payload.size());
    frame.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    frame.push_back(static_cast<char>(0x80 | payload.size()));
    frame.append(reinterpret_cast<const char*>(mask), sizeof(mask));
    for (std::size_t i = 0; i < payload.size(); ++i)
        frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));

    return ::send(fd, frame.data(), frame.size(), 0) ==
           static_cast<ssize_t>(frame.size());
}

// A masked frame with a 64-bit length header declaring `len`, carrying only the
// first `prefix` bytes of that payload.
bool send_masked_len64_header(int fd, bool fin, std::uint8_t opcode, std::uint64_t len,
                              const std::string& prefix)
{
    constexpr std::uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string frame;
    frame.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int i = 7; i >= 0; --i)
        frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    frame.append(reinterpret_cast<const char*>(mask), sizeof(mask));
    for (std::size_t i = 0; i < prefix.size(); ++i)
        frame.push_back(static_cast<char>(prefix[i] ^ mask[i % 4]));

    return ::send(fd, frame.data(), frame.size(), 0) ==
           static_cast<ssize_t>(frame.size());
}
} // namespace

TEST_CASE("test_ws_payload_overflow_e2e")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .max_payload(LIMIT)
      .onmessage([](crow::websocket::connection&, const std::string&, bool) {
          message_count++;
      })
      .onerror([](crow::websocket::connection&, const std::string&) {
          error_count++;
      });

    app.loglevel(crow::LogLevel::Critical);
    auto future = app.bindaddr("127.0.0.1").port(PORT).run_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    const int fd = connect_websocket();
    const bool connected = fd >= 0;

    // First fragment: one byte, so message_.size() becomes 1.
    const bool first_sent =
      connected && send_masked_frame(fd, false, 0x1, std::string(1, 'A'));
    // Continuation fragment declaring a 64-bit length of UINT64_MAX. The
    // unpatched check computes 1 + UINT64_MAX == 0, which is <= the 16 byte
    // limit, so the cap is bypassed and the server keeps reading the frame.
    const bool overflow_sent =
      first_sent && send_masked_len64_header(fd, true, 0x0, UINT64_MAX, std::string(64, 'B'));

    // With the fix the Mask-state check rejects the frame and onerror fires with
    // "Message length exceeds maximum payload"; without it the sum wraps under
    // the limit and the server silently keeps reading, so no error is reported.
    // Captured while the client socket is still open, so an unpatched server has
    // no end-of-stream error to confuse the signal.
    for (int i = 0; i < 40 && error_count.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const bool rejected = overflow_sent && error_count.load() > 0;
    const bool message_delivered = message_count.load() > 0;

    if (fd >= 0)
        ::close(fd);
    app.stop();
    future.wait();

    REQUIRE(connected);
    REQUIRE(overflow_sent);
    REQUIRE_FALSE(message_delivered);
    REQUIRE(rejected);
}
