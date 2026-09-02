#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "catch2/catch_all.hpp"
#include "crow.h"
#include "crow/middlewares/cors.h"

using namespace crow;

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
using asio_error_code = asio::error_code;
#endif

#define LOCALHOST_ADDRESS "127.0.0.1"

namespace
{
    bool response_complete(const std::string& data)
    {
        auto search_from = std::size_t{0};
        while (true)
        {
            const auto status_line = data.find("HTTP/1.1 ", search_from);
            if (status_line == std::string::npos)
                return false;
            const auto code = std::atoi(data.c_str() + status_line + 9);
            const auto header_end = data.find("\r\n\r\n", status_line);
            if (header_end == std::string::npos)
                return false;
            if (code >= 100 && code < 200)
            {
                search_from = header_end + 4;
                continue;
            }
            const auto length_pos = data.find("Content-Length:", status_line);
            if (length_pos == std::string::npos || length_pos > header_end)
                return true;
            const auto length = static_cast<std::size_t>(std::stoul(data.substr(length_pos + 15)));
            return data.size() >= header_end + 4 + length;
        }
    }

    class TestClient
    {
    public:
        TestClient(uint16_t port):
          socket_(io_context_)
        {
            socket_.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), port));
        }

        void send(const std::string& data)
        {
            asio::write(socket_, asio::buffer(data));
        }

        std::string receive()
        {
            std::string response;
            std::array<char, 65536> buffer{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!response_complete(response))
            {
                REQUIRE(std::chrono::steady_clock::now() < deadline);
                asio_error_code ec;
                const auto n = socket_.read_some(asio::buffer(buffer), ec);
                if (ec)
                    break;
                response.append(buffer.data(), n);
            }
            return response;
        }

    private:
        asio::io_context io_context_{};
        asio::ip::tcp::socket socket_;
    };

    int status_of(const std::string& response)
    {
        auto search_from = std::size_t{0};
        while (true)
        {
            const auto status_line = response.find("HTTP/1.1 ", search_from);
            if (status_line == std::string::npos)
                return 0;
            const auto code = std::atoi(response.c_str() + status_line + 9);
            if (code >= 100 && code < 200)
            {
                search_from = status_line + 9;
                continue;
            }
            return code;
        }
    }
} // namespace

TEST_CASE("max_body_size advertised length", "[http][max_body_size]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([&handler_ran](const request& req) {
          handler_ran = true;
          return req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    auto post = [&](const std::string& target, const std::string& payload,
                    const std::string& extra = {}) {
        TestClient client(port);
        client.send(
          "POST " + target +
          " HTTP/1.1\r\n"
          "Host: localhost\r\n" +
          extra +
          "Content-Length: " + std::to_string(payload.size()) +
          "\r\n"
          "\r\n" +
          payload);
        return client.receive();
    };

    SECTION("body of exactly max is accepted")
    {
        const auto resp = post("/upload", "12345678");
        CHECK(status_of(resp) == 200);
        CHECK(resp.find("12345678") != std::string::npos);
        CHECK(handler_ran);
    }

    SECTION("Content-Length above the cap is 413 at headers and does not run the handler")
    {
        handler_ran = false;
        const auto resp = post("/upload", "123456789");
        CHECK(status_of(resp) == 413);
        CHECK(resp.find("Connection: close") != std::string::npos);
        CHECK_FALSE(handler_ran);
    }

    SECTION("Expect 100-continue is not sent for an over-limit body")
    {
        handler_ran = false;
        TestClient client(port);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Expect: 100-continue\r\n"
          "Content-Length: 5000000\r\n"
          "\r\n");
        const auto resp = client.receive();
        CHECK(resp.find("100 Continue") == std::string::npos);
        CHECK(status_of(resp) == 413);
        CHECK_FALSE(handler_ran);
    }

    SECTION("unmatched URL still applies the app limit")
    {
        handler_ran = false;
        const auto resp = post("/missing", std::string(100, 'x'));
        CHECK(status_of(resp) == 413);
        CHECK_FALSE(handler_ran);
    }

    SECTION("wrong method still applies the app limit")
    {
        handler_ran = false;
        TestClient client(port);
        client.send(
          "PUT /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 100\r\n"
          "\r\n" +
          std::string(100, 'x'));
        const auto resp = client.receive();
        CHECK(status_of(resp) == 413);
        CHECK_FALSE(handler_ran);
    }

    app.stop();
}

TEST_CASE("max_body_size slash redirect", "[http][max_body_size]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/probe/")
      .methods("POST"_method)([&handler_ran] {
          handler_ran = true;
          return "ok";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /probe HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 100\r\n"
      "\r\n" +
      std::string(100, 'x'));
    const auto resp = client.receive();
    CHECK(status_of(resp) == 413);
    CHECK_FALSE(handler_ran);

    app.stop();
}

TEST_CASE("max_body_size chunked accumulate", "[http][max_body_size]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([&handler_ran](const request&) {
          handler_ran = true;
          return "ok";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\nhello\r\n"
      "5\r\nworld\r\n"
      "0\r\n"
      "\r\n");
    const auto resp = client.receive();
    CHECK(status_of(resp) == 413);
    CHECK_FALSE(handler_ran);

    app.stop();
}

TEST_CASE("max_body_size chunked accumulate under limit succeeds", "[http][max_body_size]")
{
    SimpleApp app;
    app.max_body_size(10);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([](const request& req) {
          return req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\nhello\r\n"
      "0\r\n"
      "\r\n");
    const auto resp = client.receive();
    CHECK(status_of(resp) == 200);
    CHECK(resp.find("hello") != std::string::npos);

    app.stop();
}

TEST_CASE("max_body_size per-route override", "[http][max_body_size]")
{
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/small")
      .methods("POST"_method)([](const request& req) {
          return req.body;
      });
    CROW_ROUTE(app, "/large")
      .methods("POST"_method)
      .max_body_size(32)([](const request& req) {
          return req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    const std::string payload(16, 'A');
    {
        TestClient client(port);
        client.send(
          "POST /small HTTP/1.1\r\nHost: localhost\r\nContent-Length: 16\r\n\r\n" + payload);
        CHECK(status_of(client.receive()) == 413);
    }
    {
        TestClient client(port);
        client.send(
          "POST /large HTTP/1.1\r\nHost: localhost\r\nContent-Length: 16\r\n\r\n" + payload);
        CHECK(status_of(client.receive()) == 200);
    }

    app.stop();
}

TEST_CASE("max_body_size zero limit", "[http][max_body_size]")
{
    SimpleApp app;
    app.max_body_size(0);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([](const request& req) {
          return std::to_string(req.body.size());
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    SECTION("empty body is accepted")
    {
        TestClient client(port);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 0\r\n"
          "\r\n");
        const auto resp = client.receive();
        CHECK(status_of(resp) == 200);
        CHECK(resp.find("0") != std::string::npos);
    }

    SECTION("any non-empty body is 413")
    {
        TestClient client(port);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 1\r\n"
          "\r\n"
          "x");
        const auto resp = client.receive();
        CHECK(status_of(resp) == 413);
    }

    app.stop();
}

TEST_CASE("max_body_size default unlimited", "[http][max_body_size]")
{
    SimpleApp app;
    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([](const request& req) {
          return std::to_string(req.body.size());
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    const std::string payload(64 * 1024, 'B');
    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
      std::to_string(payload.size()) + "\r\n\r\n" + payload);
    const auto resp = client.receive();
    CHECK(status_of(resp) == 200);
    CHECK(resp.find("65536") != std::string::npos);

    app.stop();
}

TEST_CASE("max_body_size 413 runs after-handlers", "[http][max_body_size]")
{
    App<CORSHandler> app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([] {
          return "ok";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Origin: https://example.test\r\n"
      "Content-Length: 100\r\n"
      "\r\n" +
      std::string(100, 'x'));
    const auto resp = client.receive();
    CHECK(status_of(resp) == 413);
    CHECK(resp.find("Access-Control-Allow-Origin: *") != std::string::npos);

    app.stop();
}

TEST_CASE("max_body_size unlimited does not allocate advertised Content-Length", "[http][max_body_size]")
{
    SimpleApp app;
    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([](const request& req) {
          return std::to_string(req.body.size());
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    // Keep this socket open so handle_header actually sees the huge advertised
    // length. Closing immediately can hide a reserve() that only runs after
    // the server reads the headers.
    TestClient attacker(port);
    attacker.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 1125899906842624\r\n"
      "\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TestClient client(port);
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "ping");
    const auto resp = client.receive();
    CHECK(status_of(resp) == 200);
    CHECK(resp.find("4") != std::string::npos);

    app.stop();
}

TEST_CASE("max_body_size 413 skips before-handlers", "[http][max_body_size]")
{
    struct ProbeMiddleware
    {
        std::atomic<bool> before{false};
        std::atomic<bool> after{false};

        struct context
        {};

        void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/)
        {
            before = true;
        }

        void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/)
        {
            after = true;
        }
    };

    App<ProbeMiddleware> app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([] {
          return "ok";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 100\r\n"
      "\r\n" +
      std::string(100, 'x'));
    const auto resp = client.receive();
    CHECK(status_of(resp) == 413);

    auto& probe = app.get_middleware<ProbeMiddleware>();
    CHECK_FALSE(probe.before.load());
    CHECK(probe.after.load());

    probe.before = false;
    probe.after = false;
    TestClient ok_client(app.port());
    ok_client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "abcd");
    CHECK(status_of(ok_client.receive()) == 200);
    CHECK(probe.before.load());
    CHECK(probe.after.load());

    app.stop();
}

TEST_CASE("max_body_size over-limit upload can still be written and the 413 read back", "[http][max_body_size]")
{
    // Regression test: a client that writes its whole over-limit body before
    // reading must not see its write fail (e.g. with ECONNRESET). The server
    // must linger and keep draining the socket instead of closing on unread
    // bytes, which can RST a peer that is still mid-upload and lose the
    // response it already sent. A small payload cannot reproduce this: it
    // needs to be big enough that the blocking write below has to wait on
    // the server to keep reading, rather than completing into socket buffers
    // before the server has even reacted.
    SimpleApp app;
    app.max_body_size(1024);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)([](const request& req) {
          return req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    const std::string payload(5'000'000, 'x');
    TestClient client(port);
    const std::string request =
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: " +
      std::to_string(payload.size()) +
      "\r\n"
      "\r\n" +
      payload;

    REQUIRE_NOTHROW(client.send(request));
    CHECK(status_of(client.receive()) == 413);

    app.stop();
}
