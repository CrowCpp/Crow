#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#endif

#include "crow/logging.h"

namespace crow
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
#else
    using error_code = asio::error_code;
#endif
    using tcp = asio::ip::tcp;

    namespace detail
    {
        namespace socket
        {
            struct tcp_socket_options
            {
                bool no_delay{false};
                bool keep_alive{false};
                asio::socket_base::receive_buffer_size receive_buffer_size{0};
                asio::socket_base::send_buffer_size send_buffer_size{0};
                asio::socket_base::linger linger{false, 0};
                asio::socket_base::reuse_address reuse_address{false};
            };

            inline void apply_tcp_socket_options(tcp::socket& socket, const tcp_socket_options& options)
            {
                error_code ec;
                socket.set_option(tcp::no_delay(options.no_delay), ec);
                if (ec)
                {
                    CROW_LOG_WARNING << "Failed to set TCP_NODELAY: " << ec.message();
                }
                CROW_LOG_DEBUG << "TCP_NODELAY set to: " << (options.no_delay ? "true" : "false");

                socket.set_option(asio::socket_base::keep_alive(options.keep_alive), ec);
                if (ec)
                {
                    CROW_LOG_WARNING << "Failed to set SO_KEEPALIVE: " << ec.message();
                }
                CROW_LOG_DEBUG << "SO_KEEPALIVE set to: " << (options.keep_alive ? "true" : "false");

                if (options.receive_buffer_size.value() > 0)
                {
                    socket.set_option(options.receive_buffer_size, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_RCVBUF: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_RCVBUF set to: " << options.receive_buffer_size.value();
                    }
                }

                if (options.send_buffer_size.value() > 0)
                {
                    socket.set_option(options.send_buffer_size, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_SNDBUF: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_SNDBUF set to: " << options.send_buffer_size.value();
                    }
                }

                if (options.linger.enabled())
                {
                    socket.set_option(options.linger, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_LINGER: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_LINGER set to: " << (options.linger.enabled() ? "true" : "false");
                    }
                }

                socket.set_option(options.reuse_address, ec);
                if (ec)
                {
                    CROW_LOG_WARNING << "Failed to set SO_REUSEADDR: " << ec.message();
                }
                else
                {
                    CROW_LOG_DEBUG << "SO_REUSEADDR set to: " << options.reuse_address.value();
                }
            }

            template<typename Socket>
            inline void apply_tcp_socket_options(Socket&, const tcp_socket_options&)
            {
                CROW_LOG_WARNING << "This socket type does not support set TCP_NODELAY option";
            }
        } // namespace socket
    } // namespace detail
} // namespace crow
