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
#include "crow/file_body_sink.h"

using namespace crow;

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
using asio_error_code = asio::error_code;
#endif

#define LOCALHOST_ADDRESS "127.0.0.1"

#include "http_test_utils.h"
using namespace crow_test_utils;

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

    struct ThrowingWriteSink : BodySink
    {
        bool write(const char*, std::size_t) override { throw std::runtime_error("write blew up"); }
        bool finish() override { return true; }
    };

    struct ThrowingFinishSink : BodySink
    {
        bool write(const char*, std::size_t) override { return true; }
        bool finish() override { throw std::runtime_error("finish blew up"); }
    };

    // Throws something that is not a std::exception, to exercise the sink's
    // catch(...) boundary rather than a catch(std::exception&).
    struct NonStdThrowingWriteSink : BodySink
    {
        bool write(const char*, std::size_t) override { throw 42; }
        bool finish() override { return true; }
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
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/memory")
      .methods("POST"_method)([](const request& req) {
          return std::string(FileBodySink::from(req) ? "file" : "memory") + ':' + req.body;
      });

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([](const request& req) {
          auto* file = FileBodySink::from(req);
          if (!file)
              return std::string("nobody:") + req.body;
          return std::string("file:") + read_all(file->path());
      });

    CROW_ROUTE(app, "/keep")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([](const request& req) {
          auto* file = FileBodySink::from(req);
          file->keep();
          return file->path();
      });

    const auto custom_dir = dir.path / "route-dir";
    std::filesystem::create_directories(custom_dir);
    CROW_ROUTE(app, "/custom")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(custom_dir.string()))([](const request& req) {
          return FileBodySink::from(req)->path();
      });

    CROW_ROUTE(app, "/getfile")
      .body_sink(FileBodySink::factory(dir.path.string()))([](const request& req) {
          return FileBodySink::from(req) ? "file" : "nobody";
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
        // An empty *chunked* body still counts as an incoming body and opens
        // the sink, unlike Content-Length: 0.
        TestClient client(port);
        client.send(
          "POST /upload HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Transfer-Encoding: chunked\r\n"
          "\r\n"
          "0\r\n\r\n");
        CHECK(http_body(client.receive()) == "file:");
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
        CHECK(custom_path.find(custom_dir.string()) != std::string::npos);
    }

    {
        // Keep-alive: two uploads reusing the same socket both work, and the
        // first upload's file is not disturbed by the second request.
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

TEST_CASE("request_body_file keep() survives a copied request", "[http][body_file]")
{
    // Regression test for the copy trap: the flag that used to live on
    // `request` (persist_body_file_) was a plain bool copied by value, so
    // calling the "keep" operation on a copy never reached the parser's own
    // request and the file was deleted anyway. `body_sink` is a shared_ptr
    // now, so every copy shares the same underlying FileBodySink.
    TempDir dir;
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/keep-via-copy")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([](const request& req) {
          crow::request copy = req; // a distinct object, sharing body_sink
          FileBodySink::from(copy)->keep();
          return FileBodySink::from(req)->path();
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /keep-via-copy HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "abc");
    const auto path = trim_crlf(http_body(client.receive()));
    REQUIRE(std::filesystem::exists(path));
    CHECK(read_all(path) == "abc");
    std::filesystem::remove(path);

    app.stop();
}

TEST_CASE("request_body_file deleted only after the full response is sent", "[http][body_file]")
{
    // Regression test: the file used to be unlinked as soon as the *first*
    // chunk of a streamed response was written (parser_.clear() ran inside
    // the per-chunk write helper), not after the whole response. Force the
    // streaming path with a body well above the default 1MiB threshold, then
    // pause the client mid-response (without finishing the read) and check
    // the file is still on disk while more of it is still in flight.
    TempDir dir;
    SimpleApp app;
    app.max_body_size(1024 * 1024);
    app.stream_threshold(1024);

    const std::size_t response_body_size = 8 * 1024 * 1024;
    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([response_body_size](const request&) {
          return std::string(response_body_size, 'R');
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "abc");

    // Read the response headers one byte at a time so nothing beyond them is
    // consumed, note the advertised Content-Length, then read a small prefix
    // of the body and stop — leaving the server's write loop stalled with
    // most of the body still unsent.
    std::string headers;
    while (headers.find("\r\n\r\n") == std::string::npos)
        headers += client.read_some(1);
    const auto length_pos = headers.find("Content-Length:");
    REQUIRE(length_pos != std::string::npos);
    const auto content_length = static_cast<std::size_t>(std::stoull(headers.substr(length_pos + 15)));
    REQUIRE(content_length == response_body_size);

    const std::size_t prefix_size = 4096;
    client.read_some(prefix_size);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    CHECK_FALSE(directory_is_empty(dir.path));

    // Drain exactly the rest of this one response (no more, so a bound read
    // never blocks on the still-open keep-alive connection) so the server
    // can finish and the test can clean up.
    client.read_some(content_length - prefix_size);

    app.stop();
}

TEST_CASE("request_body_file_too_large", "[http][body_file]")
{
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([&handler_ran](const request&) {
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

TEST_CASE("request_body_file_too_large chunked, over limit on a sink route", "[http][body_file]")
{
    // The Content-Length fast path at headers-complete can't catch a chunked
    // body (the size isn't known yet), so this exercises the over-limit
    // check inside on_body, after the sink has already been opened.
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(8);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "a\r\n0123456789\r\n"
      "0\r\n\r\n");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 413") != std::string::npos);
    CHECK(response.find("Connection: close") != std::string::npos);
    CHECK_FALSE(handler_ran.load());

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

    app.stop();
}

TEST_CASE("request_body_file_disconnect_cleans_up", "[http][body_file]")
{
    TempDir dir;
    std::atomic<bool> handler_ran{false};
    SimpleApp app;

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([&handler_ran](const request&) {
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
          had_file = static_cast<bool>(FileBodySink::from(req));
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
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([&](const request& req) {
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

TEST_CASE("request_body_sink write failure lets a large in-flight upload finish writing before the 500 is read", "[http][body_file]")
{
    // Regression test: a client that writes its whole over-limit body before
    // reading must not see its write fail (e.g. with a broken pipe). The
    // server must linger and keep draining the socket instead of closing on
    // unread bytes, which can RST a peer that is still mid-upload and lose
    // the response it already sent. A small payload (4 bytes, as in the test
    // above) cannot reproduce this: it needs to be big enough that the
    // client's blocking send() has to wait on the server to keep reading,
    // rather than completing into socket buffers before the server has even
    // reacted.
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(64ull * 1024 * 1024);

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

    const std::string payload(5'000'000, 'x');
    TestClient client(app.port());
    const std::string http_request =
      "POST /sink HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: " +
      std::to_string(payload.size()) +
      "\r\n"
      "\r\n" +
      payload;

    REQUIRE_NOTHROW(client.send(http_request));
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 500") != std::string::npos);
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

TEST_CASE("request_body_sink finish failure sends exactly one response", "[http][body_file]")
{
    // Regression test: on_message_complete() must not fall through to
    // process_message() (a second handle() call) after reject_body() already
    // wrote the 500 for a finish() failure - that would put two status lines
    // on one connection.
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/sink")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<FailingFinishSink>();
      })([](const request&) {
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

    const auto leftover = client.read_leftover(std::chrono::milliseconds(200));
    CHECK(leftover.empty());

    app.stop();
}

TEST_CASE("request_body_sink a throwing write() or finish() is 500", "[http][body_file]")
{
    // Regression test: write()/finish() used to be called bare; an uncaught
    // exception unwound into the worker loop's catch(std::exception&),
    // logged "Worker Crash" and left the client with no response at all
    // (and a non-std::exception ended the worker thread silently).
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/throw-write")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<ThrowingWriteSink>();
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    CROW_ROUTE(app, "/throw-finish")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<ThrowingFinishSink>();
      })([&handler_ran](const request&) {
          handler_ran = true;
          return "ran";
      });

    CROW_ROUTE(app, "/throw-write-nonstd")
      .methods("POST"_method)
      .body_sink([](const request&) {
          return std::make_unique<NonStdThrowingWriteSink>();
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

    for (const std::string target : {"/throw-write", "/throw-finish", "/throw-write-nonstd"})
    {
        handler_ran = false;
        const auto response = post_fail(target);
        CHECK(response.find("HTTP/1.1 500") != std::string::npos);
        CHECK(response.find("Connection: close") != std::string::npos);
        CHECK_FALSE(handler_ran.load());
    }

    // The server is still responsive: a throw doesn't take down the worker.
    TestClient probe(port);
    probe.send(
      "POST /throw-write HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 1\r\n"
      "\r\n"
      "x");
    CHECK(probe.receive().find("HTTP/1.1 500") != std::string::npos);

    app.stop();
}

TEST_CASE("request_body_sink calling body_sink() again replaces the factory", "[http][body_file]")
{
    TempDir dir;
    auto buf = std::make_shared<std::string>();
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/last-wins")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))
      .body_sink([buf](const request&) {
          auto sink = std::make_unique<VectorSink>();
          sink->buf = buf;
          return sink;
      })([buf](const request& req) {
          return FileBodySink::from(req) ? std::string("file") : *buf;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /last-wins HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "abc");
    CHECK(http_body(client.receive()) == "abc");
    CHECK(directory_is_empty(dir.path));

    app.stop();
}

TEST_CASE("request_body_sink a factory returning nullptr keeps the body in req.body", "[http][body_file]")
{
    // A user factory declining (nullptr) is not an error: unlike the file
    // sink's own open failure, it means "use req.body as usual" for this
    // request.
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/maybe")
      .methods("POST"_method)
      .body_sink([](const request&) -> std::unique_ptr<BodySink> {
          return nullptr;
      })([&handler_ran](const request& req) {
          handler_ran = true;
          return std::string("memory:") + req.body;
      });

    auto server = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();

    TestClient client(app.port());
    client.send(
      "POST /maybe HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 5\r\n"
      "\r\n"
      "hello");
    const auto response = client.receive();
    CHECK(response.find("HTTP/1.1 200") != std::string::npos);
    CHECK(http_body(response) == "memory:hello");
    CHECK(handler_ran.load());

    app.stop();
}

TEST_CASE("request_body_sink a throwing factory is 500", "[http][body_file]")
{
    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

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

    TestClient client(app.port());
    client.send(
      "POST /throw HTTP/1.1\r\n"
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

TEST_CASE("FileBodySink::factory rejects a directory that does not exist", "[http][body_file]")
{
    TempDir dir;
    const auto missing = dir.path / "does-not-exist";
    CHECK_THROWS_AS(FileBodySink::factory(missing.string()), std::filesystem::filesystem_error);

    const auto not_a_dir = dir.path / "not-a-dir";
    {
        std::ofstream out(not_a_dir);
        out << "x";
    }
    CHECK_THROWS_AS(FileBodySink::factory(not_a_dir.string()), std::filesystem::filesystem_error);
}

TEST_CASE("request_body_file per-request open failure is 500 and leaves no file", "[http][body_file]")
{
    // The directory exists (and is required to) when the route is set up;
    // simulate it disappearing before a request actually opens a file.
    TempDir dir;
    const auto vanishing = dir.path / "vanishing";
    std::filesystem::create_directories(vanishing);
    auto factory = FileBodySink::factory(vanishing.string());
    std::filesystem::remove(vanishing);

    std::atomic<bool> handler_ran{false};
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/upload")
      .methods("POST"_method)
      .body_sink(std::move(factory))([&handler_ran](const request&) {
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
    CHECK_FALSE(std::filesystem::exists(vanishing));

    app.stop();
}

TEST_CASE("request_body_file concurrent uploads get distinct paths", "[http][body_file]")
{
    TempDir dir;
    SimpleApp app;
    app.max_body_size(1024 * 1024);

    CROW_ROUTE(app, "/keep")
      .methods("POST"_method)
      .body_sink(FileBodySink::factory(dir.path.string()))([](const request& req) {
          auto* file = FileBodySink::from(req);
          file->keep();
          return file->path();
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
