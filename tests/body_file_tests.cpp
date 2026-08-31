#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

#include "catch2/catch_all.hpp"
#include "crow.h"

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
    std::string read_all(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    bool wait_until_removed(const std::string& path, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::filesystem::exists(path))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    bool directory_is_empty(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::directory_iterator(path, ec) == std::filesystem::directory_iterator();
    }

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

    std::string http_body(const std::string& response)
    {
        auto search_from = std::size_t{0};
        while (true)
        {
            const auto status_line = response.find("HTTP/1.1 ", search_from);
            if (status_line == std::string::npos)
                return {};
            const auto code = std::atoi(response.c_str() + status_line + 9);
            const auto header_end = response.find("\r\n\r\n", status_line);
            if (header_end == std::string::npos)
                return {};
            if (code >= 100 && code < 200)
            {
                search_from = header_end + 4;
                continue;
            }
            return response.substr(header_end + 4);
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

        void send(const char* data, std::size_t size)
        {
            asio::write(socket_, asio::buffer(data, size));
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
            REQUIRE(response_complete(response));
            return response;
        }

        asio::ip::tcp::socket& socket() { return socket_; }

    private:
        asio::io_context io_context_{};
        asio::ip::tcp::socket socket_;
    };

    struct TempDir
    {
        std::filesystem::path path;

        TempDir():
          path(std::filesystem::temp_directory_path() / ("crow-body-file-tests-" + utility::random_alphanum(12)))
        {
            std::filesystem::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
} // namespace

TEST_CASE("request_body_file", "[http][body_file]")
{
    TempDir dir;
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_file_size(1024 * 1024);

    CROW_ROUTE(app, "/memory")
      .methods("POST"_method)([](const request& req) {
          return std::string(req.has_body_file() ? "file" : "memory") + ':' + req.body;
      });

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([](const request& req) {
          return std::string(req.has_body_file() ? "file:" : "memory:") +
                 std::to_string(req.body.size()) + ':' +
                 std::to_string(req.body_file_size) + ':' +
                 req.body_file_path + ':' +
                 read_all(req.body_file_path);
      });

    CROW_ROUTE(app, "/keep")
      .methods("POST"_method)
      .body_file()([](const request& req) {
          req.keep_body_file();
          return req.body_file_path;
      });

    CROW_ROUTE(app, "/custom")
      .methods("POST"_method)
      .body_file((dir.path / "route-dir").string())([](const request& req) {
          return req.body_file_path;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(45580).run_async();
    app.wait_for_server_start();

    auto post = [](const std::string& target, const std::string& payload,
                   const std::string& extra_headers = {}) {
        TestClient client(45580);
        client.send(
          "POST " + target +
          " HTTP/1.1\r\n"
          "Host: localhost\r\n" +
          extra_headers +
          "Content-Length: " + std::to_string(payload.size()) +
          "\r\n"
          "\r\n");
        if (!payload.empty())
            client.send(payload.data(), payload.size());
        return client.receive();
    };

    const auto memory = post("/memory", "hello");
    CHECK(memory.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(memory) == "memory:hello");

    const std::string payload(64 * 1024, 'A');
    const auto uploaded = post("/upload", payload);
    REQUIRE(uploaded.find("HTTP/1.1 200") != std::string::npos);
    const auto uploaded_body = http_body(uploaded);
    REQUIRE(uploaded_body.rfind("file:0:" + std::to_string(payload.size()) + ':', 0) == 0);
    const auto path_start = uploaded_body.find(':', 7) + 1;
    const auto path_end = uploaded_body.find(':', path_start);
    REQUIRE(path_end != std::string::npos);
    const auto path = uploaded_body.substr(path_start, path_end - path_start);
    CHECK(path.find(dir.path.string()) != std::string::npos);
    CHECK(uploaded_body.substr(path_end + 1) == payload);
    CHECK(wait_until_removed(path, std::chrono::seconds(2)));

    const std::string binary(std::string("a") + '\0' + "b" + '\xff' + "c");
    const auto binary_response = post("/upload", binary);
    REQUIRE(binary_response.find("HTTP/1.1 200") != std::string::npos);
    const auto binary_body = http_body(binary_response);
    CHECK(binary_body.substr(binary_body.rfind(':') + 1) == binary);

    const auto empty = post("/upload", "");
    REQUIRE(empty.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(empty).rfind("file:0:0:", 0) == 0);

    {
        TestClient client(45580);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Transfer-Encoding: chunked\r\n"
          "\r\n"
          "5\r\nhello\r\n"
          "6\r\n world\r\n"
          "0\r\n\r\n");
        const auto response = client.receive();
        REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
        CHECK(http_body(response).find("hello world") != std::string::npos);
    }

    const auto continued = post("/upload", "ping", "Expect: 100-continue\r\n");
    REQUIRE(continued.find("100 Continue") != std::string::npos);
    REQUIRE(continued.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(continued).find("ping") != std::string::npos);

    {
        const auto response = post("/keep", "abc");
        auto kept = http_body(response);
        while (!kept.empty() && (kept.back() == '\n' || kept.back() == '\r'))
            kept.pop_back();
        REQUIRE(std::filesystem::exists(kept));
        CHECK(read_all(kept) == "abc");
        std::filesystem::remove(kept);
    }

    {
        const auto response = post("/custom", "Z");
        auto custom_path = http_body(response);
        while (!custom_path.empty() && (custom_path.back() == '\n' || custom_path.back() == '\r'))
            custom_path.pop_back();
        CHECK(custom_path.find((dir.path / "route-dir").string()) != std::string::npos);
    }

    {
        TestClient client(45580);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 4\r\n"
          "\r\n"
          "one!");
        const auto first = client.receive();
        CHECK(http_body(first).find("one!") != std::string::npos);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 5\r\n"
          "\r\n"
          "two!!");
        const auto second = client.receive();
        CHECK(http_body(second).find("two!!") != std::string::npos);
    }

    app.stop();
    server.wait();
}

TEST_CASE("request_body_file_too_large", "[http][body_file]")
{
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_file_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(45581).run_async();
    app.wait_for_server_start();

    TestClient client(45581);
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 9\r\n"
      "\r\n"
      "123456789");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 413") != std::string::npos);
    CHECK_FALSE(handler_ran.load());
    CHECK(directory_is_empty(dir.path));

    app.stop();
    server.wait();
}

TEST_CASE("request_body_file_disconnect_cleans_up", "[http][body_file]")
{
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.body_file_directory(dir.path.string());

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(45582).run_async();
    app.wait_for_server_start();

    {
        TestClient client(45582);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 64\r\n"
          "\r\n"
          "partial");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool empty = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        empty = directory_is_empty(dir.path);
        if (empty)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(empty);
    CHECK_FALSE(handler_ran.load());

    app.stop();
    server.wait();
}

TEST_CASE("create_temporary_file", "[utility][body_file]")
{
    TempDir dir;
    const auto first = utility::create_temporary_file(dir.path.string());
    const auto second = utility::create_temporary_file(dir.path.string());
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(second.empty());
    CHECK(first != second);
    CHECK(std::filesystem::exists(first));
    CHECK(std::filesystem::file_size(first) == 0);
}
