#pragma once

// Shared helpers for tests that drive a running Crow server over a raw
// socket (max_body_size_tests.cpp, body_file_tests.cpp). Free functions are
// `inline` because this header is included by more than one translation
// unit linked into the same `unittest` binary.

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "catch2/catch_all.hpp"

namespace crow_test_utils
{
    // True once `data` holds a full response (status line through body, per
    // Content-Length; a response with none is complete at the header
    // terminator). Skips past any 1xx interim responses (e.g. 100 Continue).
    inline bool response_complete(const std::string& data)
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
            const auto length = static_cast<std::size_t>(std::stoull(data.substr(length_pos + 15)));
            return data.size() >= header_end + 4 + length;
        }
    }

    // The status code of the last non-interim response in `response`.
    inline int status_of(const std::string& response)
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

    // The body of the last non-interim response in `response`.
    inline std::string http_body(const std::string& response)
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

    // A bare-socket HTTP client, for tests that need to control framing
    // (partial sends, reading mid-response, checking what's left on the wire)
    // more precisely than a real HTTP client would allow.
    //
    // Relies on `asio`/`asio_error_code` and `LOCALHOST_ADDRESS` already
    // being visible at the point this header is included (each including
    // .cpp sets those up first, matching the CROW_USE_BOOST/standalone asio
    // switch already in effect for the rest of the file).
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

        // Reads exactly `size` more bytes into an internal buffer without
        // waiting for the response to complete. Used to pause a client
        // mid-response and observe server-side state while it is still
        // streaming.
        std::string read_some(std::size_t size)
        {
            std::string out(size, '\0');
            asio::read(socket_, asio::buffer(out));
            return out;
        }

        // Collects whatever bytes show up within `budget`, to check for a
        // second response wrongly sent after a first one is already complete.
        std::string read_leftover(std::chrono::milliseconds budget)
        {
            std::string leftover;
            std::array<char, 65536> buffer{};
            socket_.non_blocking(true);
            const auto deadline = std::chrono::steady_clock::now() + budget;
            while (std::chrono::steady_clock::now() < deadline)
            {
                asio_error_code ec;
                const auto n = socket_.read_some(asio::buffer(buffer), ec);
                if (!ec && n > 0)
                    leftover.append(buffer.data(), n);
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return leftover;
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
} // namespace crow_test_utils
