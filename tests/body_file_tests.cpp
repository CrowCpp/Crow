#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif

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

    bool directory_is_empty(const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto it = std::filesystem::directory_iterator(path, ec);
        if (ec)
            return false;
        return it == std::filesystem::directory_iterator();
    }

    bool no_crow_body_files(const std::filesystem::path& path)
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec))
        {
            if (ec)
                return false;
            if (entry.is_regular_file() &&
                entry.path().filename().string().rfind("crow-body-", 0) == 0)
                return false;
        }
        return true;
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
            return response;
        }

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

    struct VectorSink : BodySink
    {
        std::shared_ptr<std::string> buf;
        bool write(const char* data, std::size_t length) override
        {
            buf->append(data, length);
            return true;
        }
        bool finish() override { return true; }
    };

    struct FailingWriteSink : BodySink
    {
        bool write(const char*, std::size_t) override { return false; }
        bool finish() override { return true; }
    };

    struct FailingFinishSink : BodySink
    {
        bool write(const char*, std::size_t) override { return true; }
        bool finish() override { return false; }
    };

    std::string trim_crlf(std::string s)
    {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        return s;
    }
} // namespace

TEST_CASE("request_body_file", "[http][body_file]")
{
    TempDir dir;
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/memory")
      .methods("POST"_method)([](const request& req) {
          return std::string(req.has_body_file() ? "file" : "memory") + ':' + req.body;
      });

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([](const request& req) {
          if (!req.has_body_file())
              return std::string("nobody:") + req.body;
          return std::string("file:") + read_all(req.body_file_path);
      });

    CROW_ROUTE(app, "/keep")
      .methods("POST"_method)
      .body_file()([](const request& req) {
          return req.take_body_file();
      });

    CROW_ROUTE(app, "/custom")
      .methods("POST"_method)
      .body_file((dir.path / "route-dir").string())([](const request& req) {
          return req.body_file_path;
      });

    CROW_ROUTE(app, "/getfile")
      .body_file()([](const request& req) {
          return req.has_body_file() ? "file" : "nobody";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    auto post = [&](const std::string& target, const std::string& payload,
                    const std::string& extra_headers = {}) {
        TestClient client(port);
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
    CHECK(http_body(uploaded) == "file:" + payload);

    const std::string binary(std::string("a") + '\0' + "b" + '\xff' + "c");
    const auto binary_response = post("/upload", binary);
    REQUIRE(binary_response.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(binary_response) == "file:" + binary);

    const auto empty = post("/upload", "");
    REQUIRE(empty.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(empty) == "nobody:");

    {
        TestClient client(port);
        client.send(
          "GET /getfile HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "\r\n");
        CHECK(http_body(client.receive()) == "nobody");
        CHECK(no_crow_body_files(dir.path));
    }

    {
        TestClient client(port);
        client.send(
          "GET /getfile HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 0\r\n"
          "\r\n");
        CHECK(http_body(client.receive()) == "nobody");
        CHECK(no_crow_body_files(dir.path));
    }

    {
        TestClient client(port);
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
        CHECK(http_body(response) == "file:hello world");
    }

    const auto continued = post("/upload", "ping", "Expect: 100-continue\r\n");
    REQUIRE(continued.find("100 Continue") != std::string::npos);
    REQUIRE(continued.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(continued) == "file:ping");

    {
        const auto response = post("/keep", "abc");
        const auto kept = trim_crlf(http_body(response));
        REQUIRE(std::filesystem::exists(kept));
        CHECK(read_all(kept) == "abc");
#ifndef _WIN32
        struct stat st {};
        REQUIRE(::stat(kept.c_str(), &st) == 0);
        CHECK((st.st_mode & 0777) == 0600);
#endif
        std::filesystem::remove(kept);
    }

    {
        const auto response = post("/custom", "Z");
        const auto custom_path = trim_crlf(http_body(response));
        CHECK(custom_path.find((dir.path / "route-dir").string()) != std::string::npos);
    }

    {
        TestClient client(port);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 4\r\n"
          "\r\n"
          "one!");
        const auto first = client.receive();
        CHECK(http_body(first) == "file:one!");
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 5\r\n"
          "\r\n"
          "two!!");
        const auto second = client.receive();
        CHECK(http_body(second) == "file:two!!");
    }

    const auto upload_path_resp = post("/keep", payload);
    const auto first_path = trim_crlf(http_body(upload_path_resp));
    const auto second_keep = post("/keep", "x");
    const auto second_path = trim_crlf(http_body(second_keep));
    CHECK(first_path != second_path);
    std::filesystem::remove(first_path);
    std::filesystem::remove(second_path);

    app.stop();
}

TEST_CASE("request_body_file_too_large", "[http][body_file]")
{
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 9\r\n"
      "\r\n"
      "123456789");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 413") != std::string::npos);
    CHECK(response.find("Connection: close") != std::string::npos);
    CHECK_FALSE(handler_ran.load());
    CHECK(directory_is_empty(dir.path));

    app.stop();
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

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    {
        TestClient client(app.port());
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
}

TEST_CASE("request_body_sink", "[http][body_file]")
{
    auto buf = std::make_shared<std::string>();
    std::atomic<std::size_t> body_size{999};
    std::atomic<std::size_t> body_capacity{999};
    std::atomic<bool> had_file{true};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/sink")
      .methods("POST"_method)
      .body_sink([buf](const request&) {
          auto sink = std::make_unique<VectorSink>();
          sink->buf = buf;
          return sink;
      })([buf, &body_size, &body_capacity, &had_file](const request& req) {
          body_size = req.body.size();
          body_capacity = req.body.capacity();
          had_file = req.has_body_file();
          return req.body.empty() ? *buf : req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    const std::string payload(32 * 1024, 'S');
    TestClient client(app.port());
    client.send(
      "POST /sink HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
      std::to_string(payload.size()) + "\r\n\r\n" + payload);
    CHECK(http_body(client.receive()) == payload);
    CHECK(body_size.load() == 0);
    CHECK(body_capacity.load() < payload.size());
    CHECK_FALSE(had_file.load());

    app.stop();
}

TEST_CASE("request_body_file does not reserve req.body", "[http][body_file]")
{
    TempDir dir;
    std::atomic<std::size_t> body_size{999};
    std::atomic<std::size_t> body_capacity{999};
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file()([&](const request& req) {
          body_size = req.body.size();
          body_capacity = req.body.capacity();
          return "ok";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    const std::string payload(64 * 1024, 'A');
    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: " +
      std::to_string(payload.size()) +
      "\r\n"
      "\r\n");
    client.send(payload.data(), payload.size());
    const auto response = client.receive();
    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
    CHECK(body_size.load() == 0);
    CHECK(body_capacity.load() < payload.size());

    app.stop();
}

TEST_CASE("request_body_sink write failure is 500", "[http][body_file]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/sink")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<FailingWriteSink>();
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /sink HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "fail");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 500") != std::string::npos);
    CHECK(response.find("Connection: close") != std::string::npos);
    CHECK_FALSE(handler_ran.load());

    app.stop();
}

TEST_CASE("request_body_sink finish failure is 500", "[http][body_file]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/sink")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<FailingFinishSink>();
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /sink HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "fail");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 500") != std::string::npos);
    CHECK(response.find("Connection: close") != std::string::npos);
    CHECK_FALSE(handler_ran.load());

    app.stop();
}

TEST_CASE("request_body_sink last call wins over body_file", "[http][body_file]")
{
    TempDir dir;
    auto buf = std::make_shared<std::string>();
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/sink-last")
      .methods("POST"_method)
      .body_file()
      .body_sink([buf](const request&) {
          auto sink = std::make_unique<VectorSink>();
          sink->buf = buf;
          return sink;
      })([buf](const request& req) {
          return req.has_body_file() ? std::string("file") : *buf;
      });

    CROW_ROUTE(app, "/file-last")
      .methods("POST"_method)
      .body_sink([buf](const request&) {
          auto sink = std::make_unique<VectorSink>();
          sink->buf = buf;
          return sink;
      })
      .body_file()([](const request& req) {
          return req.has_body_file() ? std::string("file:") + read_all(req.body_file_path) : std::string("sink");
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    {
        TestClient client(port);
        client.send(
          "POST /sink-last HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 3\r\n"
          "\r\n"
          "abc");
        CHECK(http_body(client.receive()) == "abc");
        CHECK(directory_is_empty(dir.path));
    }

    {
        TestClient client(port);
        client.send(
          "POST /file-last HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 3\r\n"
          "\r\n"
          "xyz");
        CHECK(http_body(client.receive()) == "file:xyz");
    }

    app.stop();
}

TEST_CASE("request_body_sink open failure is 500", "[http][body_file]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/null")
      .methods("POST"_method)
      .body_sink([](const request&) -> std::unique_ptr<BodySink> {
          return nullptr;
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    CROW_ROUTE(app, "/throw")
      .methods("POST"_method)
      .body_sink([](const request&) -> std::unique_ptr<BodySink> {
          throw std::runtime_error("sink open");
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    auto post_fail = [&](const std::string& target) {
        TestClient client(port);
        client.send(
          "POST " + target +
          " HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Content-Length: 4\r\n"
          "\r\n"
          "fail");
        return client.receive();
    };

    {
        const auto response = post_fail("/null");
        CHECK(response.find("HTTP/1.1 500") != std::string::npos);
        CHECK(response.find("Connection: close") != std::string::npos);
        CHECK_FALSE(handler_ran.load());
    }

    handler_ran = false;
    {
        const auto response = post_fail("/throw");
        CHECK(response.find("HTTP/1.1 500") != std::string::npos);
        CHECK(response.find("Connection: close") != std::string::npos);
        CHECK_FALSE(handler_ran.load());
    }

    app.stop();
}

TEST_CASE("request_body_file open failure is 500 and leaves no file", "[http][body_file]")
{
    TempDir dir;
    const auto not_a_dir = dir.path / "not-a-dir";
    {
        std::ofstream out(not_a_dir);
        out << "x";
    }
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_file(not_a_dir.string())([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 4\r\n"
      "\r\n"
      "fail");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 500") != std::string::npos);
    CHECK(response.find("Connection: close") != std::string::npos);
    CHECK_FALSE(handler_ran.load());

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path, ec))
    {
        CHECK(entry.path().filename() == "not-a-dir");
    }

    app.stop();
}

TEST_CASE("request_body_file concurrent uploads get distinct paths", "[http][body_file]")
{
    TempDir dir;
    SimpleApp app;
    app.body_file_directory(dir.path.string()).max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/keep")
      .methods("POST"_method)
      .body_file()([](const request& req) {
          return req.take_body_file();
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    const auto port = app.port();

    TestClient a(port);
    TestClient b(port);
    a.send(
      "POST /keep HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 8\r\n"
      "\r\n"
      "AAAA");
    b.send(
      "POST /keep HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 8\r\n"
      "\r\n"
      "BBBB");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::size_t nfiles = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        nfiles = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir.path, ec))
        {
            if (!ec && entry.is_regular_file())
                ++nfiles;
        }
        if (nfiles >= 2)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(nfiles >= 2);

    a.send("aaaa");
    b.send("bbbb");
    const auto path_a = trim_crlf(http_body(a.receive()));
    const auto path_b = trim_crlf(http_body(b.receive()));
    CHECK(path_a != path_b);
    CHECK(read_all(path_a) == "AAAAaaaa");
    CHECK(read_all(path_b) == "BBBBbbbb");
    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);

    app.stop();
}
