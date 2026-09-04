#pragma once

// Helpers shared between the unit test translation units.

#include <chrono>
#include <future>
#include <string>

#include "crow.h"

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
using asio_error_code = asio::error_code;
#endif

#define LOCALHOST_ADDRESS "127.0.0.1"

/** simple http client class for making client requests */
class HttpClient
{
private:
    asio::io_context ic{};
    asio::ip::tcp::socket c;

public:
    /** construct an instance by address and port */
    HttpClient(std::string const& address, uint16_t port):
      c(ic)
    {
        c.connect(asio::ip::tcp::endpoint( asio::ip::make_address(address),
                                           port));
    }

    /** sends a request string through the socket */
    void send(const std::string& msg)
    {
        c.send(asio::buffer(msg));
    }

    /** sends a request string through the socket */
    void send(const char* const msg, size_t msg_size)
    {
        c.send(asio::buffer(msg, msg_size));
    }


    /** method shall be called after sending a request with send
     * @returns the received response string */
    std::string receive()
    {
        char buf[2048];
        auto received = c.receive(asio::buffer(buf, sizeof(buf)));
        std::string rval(buf, received);
        return rval;
    }

    asio::ip::tcp::socket& socket()
    {
        return c;
    }

    /** static method for making a request
     * @returns the received response string */
    static std::string request(const std::string& address,
                               uint16_t port,
                               const std::string& sendmsg)
    {
        HttpClient c(address, port);
        c.send(sendmsg);
        return c.receive();
    }
};

class BoundedServerShutdown {
public:
    BoundedServerShutdown(std::future<void>& server_task, std::function<void()> stop)
        : server_task_(server_task)
        , stop_(std::move(stop)) {
    }

    ~BoundedServerShutdown() {
        shutdown();
    }

    void shutdown() noexcept {
        if (stopped_)
            return;

        stopped_ = true;
        try {
            stop_();
        } catch (...) {
            std::terminate();
        }

        if (server_task_.valid() && server_task_.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            std::terminate();
    }

private:
    std::future<void>& server_task_;
    std::function<void()> stop_;
    bool stopped_{false};
};

template<typename Predicate>
bool receive_with_deadline(asio::ip::tcp::socket& socket,
                           std::string& response,
                           std::chrono::milliseconds timeout,
                           Predicate complete,
                           bool* peer_closed = nullptr) {
    if (peer_closed)
        *peer_closed = false;
    if (complete(response))
        return true;

    auto& io_context = GET_IO_CONTEXT(socket);
#if (defined(CROW_USE_BOOST) && BOOST_VERSION >= 107000) || (ASIO_VERSION >= 101008)
    io_context.restart();
#else
    io_context.reset();
#endif
    asio::steady_timer timer(io_context);
    std::array<char, 65536> buffer;
    bool timed_out = false;
    std::function<void()> read_next;

    read_next = [&] {
        socket.async_read_some(asio::buffer(buffer), [&](const asio_error_code& ec, std::size_t received) {
            if (!ec) {
                response.append(buffer.data(), received);
                if (complete(response)) {
                    timer.cancel();
                } else {
                    read_next();
                }
                return;
            }

            if (!timed_out && peer_closed)
                *peer_closed = true;
            timer.cancel();
        });
    };

    timer.expires_after(timeout);
    timer.async_wait([&](const asio_error_code& ec) {
        if (ec)
            return;

        timed_out = true;
        asio_error_code cancel_error;
        socket.cancel(cancel_error);
    });
    read_next();
    io_context.run();

    return complete(response);
}

inline bool receive_until_closed_with_deadline(asio::ip::tcp::socket& socket,
                                        std::string& response,
                                        std::chrono::milliseconds timeout) {
    bool peer_closed = false;
    receive_with_deadline(socket, response, timeout, [](const std::string&) { return false; }, &peer_closed);
    return peer_closed;
}
