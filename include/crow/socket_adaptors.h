#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#include <boost/asio/version.hpp>
#ifdef CROW_ENABLE_SSL
#include <boost/asio/ssl.hpp>
#endif
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#include <asio/version.hpp>
#ifdef CROW_ENABLE_SSL
#include <asio/ssl.hpp>
#endif
#endif
#include "crow/settings.h"

#if (defined(CROW_USE_BOOST) && BOOST_VERSION >= 107000) || (ASIO_VERSION >= 101008)
#define GET_IO_CONTEXT(s) ((asio::io_context&)(s).get_executor().context())
#else
#define GET_IO_CONTEXT(s) ((s).get_io_service())
#endif

namespace crow
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
#else
    using error_code = asio::error_code;
#endif
    using tcp = asio::ip::tcp;
    using stream_protocol = asio::local::stream_protocol;

    /// A wrapper for the asio::ip::tcp::socket and asio::ssl::stream
    struct SocketAdaptor
    {
        using context = void;
        SocketAdaptor(asio::io_context& io_context, context*):
          socket_(io_context)
        {}

        asio::io_context& get_io_context()
        {
            return GET_IO_CONTEXT(socket_);
        }

        /// Get the TCP socket handling data transfers, regardless of what layer is handling transfers on top of the socket.
        tcp::socket& raw_socket()
        {
            return socket_;
        }

        /// Get the object handling data transfers, this can be either a TCP socket or an SSL stream (if SSL is enabled).
        tcp::socket& socket()
        {
            return socket_;
        }

        tcp::endpoint remote_endpoint() const
        {
            return socket_.remote_endpoint();
        }

        std::string address() const
        {
            return socket_.remote_endpoint().address().to_string();
        }

        bool is_open() const
        {
            return socket_.is_open();
        }

        void close()
        {
            error_code ec;
            socket_.close(ec);
        }

        void shutdown_readwrite()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);
        }

        void shutdown_write()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_send, ec);
        }

        void shutdown_read()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_receive, ec);
        }

        template<typename F>
        void start(F f)
        {
            f(error_code());
        }

        tcp::socket socket_;
    };

    struct UnixSocketAdaptor
    {
        using context = void;
        UnixSocketAdaptor(asio::io_context& io_context, context*):
          socket_(io_context)
        {
        }

        asio::io_context& get_io_context()
        {
            return GET_IO_CONTEXT(socket_);
        }

        stream_protocol::socket& raw_socket()
        {
            return socket_;
        }

        stream_protocol::socket& socket()
        {
            return socket_;
        }

        stream_protocol::endpoint remote_endpoint()
        {
            return socket_.local_endpoint();
        }

        std::string address() const
        {
            return "";
        }

        bool is_open()
        {
            return socket_.is_open();
        }

        void close()
        {
            error_code ec;
            socket_.close(ec);
        }

        void shutdown_readwrite()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);
        }

        void shutdown_write()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_send, ec);
        }

        void shutdown_read()
        {
            error_code ec;
            socket_.shutdown(asio::socket_base::shutdown_type::shutdown_receive, ec);
        }

        template<typename F>
        void start(F f)
        {
            f(error_code());
        }

        stream_protocol::socket socket_;
    };

#ifdef CROW_ENABLE_SSL
    struct SSLAdaptor
    {
        using context = asio::ssl::context;
        using ssl_socket_t = asio::ssl::stream<tcp::socket>;
        SSLAdaptor(asio::io_context& io_context, context* ctx):
          ssl_socket_(new ssl_socket_t(io_context, *ctx))
        {}

        asio::ssl::stream<tcp::socket>& socket()
        {
            return *ssl_socket_;
        }

        tcp::socket::lowest_layer_type&
          raw_socket()
        {
            return ssl_socket_->lowest_layer();
        }

        tcp::endpoint remote_endpoint()
        {
            return raw_socket().remote_endpoint();
        }

        std::string address() const
        {
            return ssl_socket_->lowest_layer().remote_endpoint().address().to_string();
        }

        bool is_open()
        {
            return ssl_socket_ ? raw_socket().is_open() : false;
        }

        void close()
        {
            if (is_open())
            {
                error_code ec;
                raw_socket().close(ec);
            }
        }

        void shutdown_readwrite()
        {
            if (is_open())
            {
                error_code ec;
                raw_socket().shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);
            }
        }

        void shutdown_write()
        {
            if (is_open())
            {
                error_code ec;
                raw_socket().shutdown(asio::socket_base::shutdown_type::shutdown_send, ec);
            }
        }

        void shutdown_read()
        {
            if (is_open())
            {
                error_code ec;
                raw_socket().shutdown(asio::socket_base::shutdown_type::shutdown_receive, ec);
            }
        }

        asio::io_context& get_io_context()
        {
            return GET_IO_CONTEXT(raw_socket());
        }

        template<typename F>
        void start(F f)
        {
            ssl_socket_->async_handshake(asio::ssl::stream_base::server,
                                         [f](const error_code& ec) {
                                             f(ec);
                                         });
        }

        std::unique_ptr<asio::ssl::stream<tcp::socket>> ssl_socket_;
    };
#endif
} // namespace crow
