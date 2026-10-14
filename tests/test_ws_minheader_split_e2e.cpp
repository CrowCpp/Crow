/**
* Crow WebSocket mini-header short-read desync.
*
* The 2-byte frame header was read with async_read_some, which can complete
* after a single byte when the two header bytes arrive in separate TCP
* segments. The handler then treated mini_header_ as complete and resumed
* reading mid-frame, desynchronizing the stream.
*
* Loopback only. The client sends one masked text frame "hello" but splits
* the write between the first and second header byte. A correct server waits
* for both header bytes and delivers "hello" once; the buggy server parses a
* half-header, dispatches a spurious empty message and loses the real one.
*/

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

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

std::atomic<int> message_count{0};
std::mutex received_mutex;
std::string received_message;

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
} // namespace

TEST_CASE("test_ws_minheader_split_e2e")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onmessage([](crow::websocket::connection&, const std::string& data, bool) {
          {
              std::lock_guard<std::mutex> lock(received_mutex);
              received_message = data;
          }
          message_count++;
      });

    app.loglevel(crow::LogLevel::Critical);
    auto future = app.bindaddr("127.0.0.1").port(PORT).run_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    const int fd = connect_websocket();
    REQUIRE(fd >= 0);

    // Masked text frame carrying "hello" (FIN|text, len 5).
    const std::string payload = "hello";
    constexpr std::uint8_t mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string header;
    header.push_back(static_cast<char>(0x80 | 0x1));            // FIN | text frame
    header.push_back(static_cast<char>(0x80 | payload.size())); // mask bit | length
    std::string rest(reinterpret_cast<const char*>(mask), sizeof(mask));
    for (std::size_t i = 0; i < payload.size(); ++i)
        rest.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));

    // Split the write between the first and second header byte so the two
    // header bytes reach the server in separate reads.
    ::send(fd, header.data(), 1, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ::send(fd, header.data() + 1, 1, 0);
    ::send(fd, rest.data(), rest.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::string observed;
    {
        std::lock_guard<std::mutex> lock(received_mutex);
        observed = received_message;
    }

    ::close(fd);
    app.stop();
    future.wait();

    REQUIRE(message_count.load() == 1);
    REQUIRE(observed == payload);
}
