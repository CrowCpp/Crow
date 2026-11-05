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
            };

            inline void apply_tcp_socket_options(tcp::socket& socket, const tcp_socket_options& options)
            {
                error_code ec;
                socket.set_option(tcp::no_delay(options.no_delay), ec);
                if (ec)
                {
                    CROW_LOG_WARNING << "Failed to set TCP_NODELAY: " << ec.message();
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
