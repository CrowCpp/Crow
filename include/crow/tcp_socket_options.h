#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#endif

#include <type_traits>

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

            template<typename Socket>
            inline void apply_tcp_socket_options(Socket& socket, const tcp_socket_options& options)
            {
                auto& lowest = socket.lowest_layer();
                using protocol_type = typename std::remove_reference_t<decltype(lowest)>::protocol_type;

                if constexpr (std::is_same_v<protocol_type, tcp>)
                {
                    error_code ec;
                    lowest.set_option(tcp::no_delay(options.no_delay), ec);
                    if (ec)
                    {
                        CROW_LOG_WARNING << "Failed to set TCP_NODELAY: " << ec.message();
                    }
                }
                else if (options.no_delay)
                {
                    CROW_LOG_WARNING << "TCP_NODELAY requested but not supported for this socket type";
                }
            }
        } // namespace socket
    } // namespace detail
} // namespace crow
