// Crow WebSocket maximum-payload bypass through fragmented messages.
//
// Build:
//   g++ -std=c++17 -I <crow>/include -I <asio>/asio/include \
//       -DASIO_STANDALONE -O1 -o ws_fragment_limit_e2e \
//       ws_fragment_limit_e2e.cpp -lpthread
//
// Loopback only. The configured maximum message payload is 16 bytes.

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0
#include "crow.h"

#include "catch2/catch_all.hpp"

namespace
{
constexpr int PORT = 41809;
constexpr std::uint64_t LIMIT = 16;

std::atomic<int> message_count{0};
std::atomic<int> error_count{0};
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
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

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
} // namespace

TEST_CASE("test_ws_fragment_limit_e2e")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .max_payload(LIMIT)
      .onmessage([](crow::websocket::connection&, const std::string& data, bool) {
          {
              std::lock_guard<std::mutex> lock(received_mutex);
              received_message = data;
          }
          message_count++;
      })
      .onerror([](crow::websocket::connection&, const std::string&) {
          error_count++;
      });

    app.loglevel(crow::LogLevel::Critical);
    auto future = app.bindaddr("127.0.0.1").port(PORT).run_async();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    // Control: one 24-byte frame exceeds the 16-byte limit and must be rejected.
    const int control_fd = connect_websocket();
    const bool control_connected = control_fd >= 0;
    REQUIRE(control_connected);

    const bool control_sent =
      control_connected && send_masked_frame(control_fd, true, 0x1, std::string(24, 'C'));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const bool control_rejected =
      control_sent && message_count.load() == 0 && error_count.load() > 0;
    REQUIRE(message_count.load() == 0);
    REQUIRE(error_count.load() > 0);
    if (control_fd >= 0)
        ::close(control_fd);

    // Test: two 12-byte frames are each below the limit, but the complete
    // fragmented message is 24 bytes and should be rejected as a whole.
    const int fragmented_fd = connect_websocket();
    const bool fragmented_connected = fragmented_fd >= 0;

    REQUIRE(fragmented_fd >= 0);

    const bool first_sent =
      fragmented_connected && send_masked_frame(fragmented_fd, false, 0x1, std::string(12, 'A'));
    const bool second_sent =
      first_sent && send_masked_frame(fragmented_fd, true, 0x0, std::string(12, 'B'));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::string observed;
    {
        std::lock_guard<std::mutex> lock(received_mutex);
        observed = received_message;
    }
    const bool oversized_message_delivered =
      second_sent && message_count.load() == 1 &&
      observed == std::string(12, 'A') + std::string(12, 'B');
    REQUIRE(second_sent);
    REQUIRE(message_count.load() == 0);
    REQUIRE(observed=="");
    REQUIRE_FALSE(oversized_message_delivered);

    if (fragmented_fd >= 0)
        ::close(fragmented_fd);

    app.stop();
    future.wait();

    CROW_LOG_DEBUG << "CONFIGURED_MAX_PAYLOAD=" << LIMIT << "\n"
              << "CONTROL_SINGLE_FRAME_BYTES=24\n"
              << "CONTROL_SINGLE_FRAME_REJECTED=" << control_rejected << "\n"
              << "FRAGMENT_ONE_BYTES=12\n"
              << "FRAGMENT_TWO_BYTES=12\n"
              << "FRAGMENTED_MESSAGE_BYTES=" << observed.size() << "\n"
              << "FRAGMENTED_MESSAGE_DELIVERED=" << oversized_message_delivered << "\n"
              << "MESSAGE_HANDLER_CALLS=" << message_count.load() << "\n"
              << "ERROR_HANDLER_CALLS=" << error_count.load() << "\n";

    const bool confirmed = control_connected && control_rejected &&
                           fragmented_connected && oversized_message_delivered;
    CROW_LOG_DEBUG << "VERDICT="
              << (confirmed ? "WEBSOCKET_FRAGMENTED_MESSAGE_LIMIT_BYPASS_CONFIRMED"
                            : "NOT_CONFIRMED");
    REQUIRE_FALSE(confirmed);
}
