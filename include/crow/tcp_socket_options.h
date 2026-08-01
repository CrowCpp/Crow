#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#endif

#include <optional>

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
                std::optional<bool> no_delay;
                std::optional<bool> keep_alive;
                std::optional<asio::socket_base::receive_buffer_size> receive_buffer_size;
                std::optional<asio::socket_base::send_buffer_size> send_buffer_size;
                std::optional<asio::socket_base::linger> linger;
                std::optional<asio::socket_base::reuse_address> reuse_address;  // Applied on TCP acceptors (before bind).
                std::optional<bool> v6_only;                                    // Applied on TCP acceptors (before bind).
                std::optional<bool> enable_connection_aborted;                  // Applied on TCP acceptors (before async_accept).
                std::optional<int> listen_backlog;                              // Controls the listen backlog used by acceptor.listen().
            };

            inline void apply_tcp_socket_options(tcp::socket& socket, const tcp_socket_options& options)
            {
                error_code ec;
                if (options.no_delay)
                {
                    socket.set_option(tcp::no_delay(*options.no_delay), ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set TCP_NODELAY: " << ec.message();
                    }
                    CROW_LOG_DEBUG << "TCP_NODELAY set to: " << (*options.no_delay ? "true" : "false");
                }

                if (options.keep_alive)
                {
                    socket.set_option(asio::socket_base::keep_alive(*options.keep_alive), ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_KEEPALIVE: " << ec.message();
                    }
                    CROW_LOG_DEBUG << "SO_KEEPALIVE set to: " << (*options.keep_alive ? "true" : "false");
                }

                if (options.receive_buffer_size)
                {
                    socket.set_option(*options.receive_buffer_size, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_RCVBUF: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_RCVBUF set to: " << options.receive_buffer_size->value();
                    }
                }

                if (options.send_buffer_size)
                {
                    socket.set_option(*options.send_buffer_size, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_SNDBUF: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_SNDBUF set to: " << options.send_buffer_size->value();
                    }
                }

                if (options.linger)
                {
                    socket.set_option(*options.linger, ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set SO_LINGER: " << ec.message();
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "SO_LINGER set to: " << (options.linger->enabled() ? "true" : "false");
                    }
                }

            }

            inline bool apply_acceptor_socket_options(tcp::acceptor& acceptor, const tcp_socket_options& options, bool default_reuse_address)
            {
                const bool reuse_address = options.reuse_address ? options.reuse_address->value() : default_reuse_address;

                error_code ec;
                acceptor.set_option(tcp::acceptor::reuse_address(reuse_address), ec);
                if (ec)
                {
                    CROW_LOG_WARNING << "Failed to set SO_REUSEADDR on acceptor: " << ec.message();
                    return false;
                }
                CROW_LOG_DEBUG << "SO_REUSEADDR set on acceptor to: " << (reuse_address ? "true" : "false");

                if (options.v6_only)
                {
                    acceptor.set_option(asio::ip::v6_only(*options.v6_only), ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set IPV6_V6ONLY on acceptor: " << ec.message();
                        return false;
                    }
                    CROW_LOG_DEBUG << "IPV6_V6ONLY set on acceptor to: " << (*options.v6_only ? "true" : "false");
                }

                if (options.enable_connection_aborted)
                {
                    acceptor.set_option(asio::socket_base::enable_connection_aborted(*options.enable_connection_aborted), ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set enable_connection_aborted on acceptor: " << ec.message();
                        return false;
                    }
                    CROW_LOG_DEBUG << "enable_connection_aborted set on acceptor to: " << (*options.enable_connection_aborted ? "true" : "false");
                }

                CROW_LOG_DEBUG << "SO_REUSEADDR set on acceptor to: " << reuse_address;
                return true;
            }

            template<typename Acceptor>
            inline bool apply_acceptor_socket_options(Acceptor&, const tcp_socket_options& options, bool default_reuse_address)
            {
                if (options.reuse_address && options.reuse_address->value() != default_reuse_address)
                {
                    CROW_LOG_WARNING << "This acceptor type does not support applying SO_REUSEADDR";
                }

                if (options.v6_only)
                {
                    CROW_LOG_WARNING << "This acceptor type does not support applying IPV6_V6ONLY";
                }

                if (options.enable_connection_aborted)
                {
                    CROW_LOG_WARNING << "This acceptor type does not support applying enable_connection_aborted";
                }

                CROW_LOG_INFO << "This acceptor type does not support applying TCP socket options";
                return true;
            }

            inline int resolve_acceptor_listen_backlog(const tcp_socket_options& options, int default_listen_backlog)
            {
                if (!options.listen_backlog || *options.listen_backlog <= 0) {
                    CROW_LOG_INFO << "Using default listen backlog: " << default_listen_backlog;
                    return default_listen_backlog;
                }

                CROW_LOG_INFO << "Using configured listen backlog: " << *options.listen_backlog;
                return *options.listen_backlog;
            }

            template<typename Socket>
            inline void apply_tcp_socket_options(Socket&, const tcp_socket_options&)
            {
                CROW_LOG_WARNING << "This socket type does not support applying TCP socket options";
            }
        } // namespace socket
    } // namespace detail
} // namespace crow
