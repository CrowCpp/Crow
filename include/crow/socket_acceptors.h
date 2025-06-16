#pragma once
#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#ifdef CROW_ENABLE_SSL
#include <boost/asio/ssl.hpp>
#endif
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#ifdef CROW_ENABLE_SSL
#include <asio/ssl.hpp>
#endif
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
    using stream_protocol = asio::local::stream_protocol;

    struct TCPAcceptor
    {
        using endpoint = tcp::endpoint;
        tcp::acceptor acceptor_;
        TCPAcceptor(asio::io_context& io_context):
          acceptor_(io_context) {}

        int16_t port() const
        {
            return acceptor_.local_endpoint().port();
        }
        std::string address() const
        {
            return acceptor_.local_endpoint().address().to_string();
        }
        std::string url_display(bool ssl_used) const
        {
            return (ssl_used ? "https://" : "http://") + acceptor_.local_endpoint().address().to_string() + ":" + std::to_string(acceptor_.local_endpoint().port());
        }
        tcp::acceptor& raw_acceptor()
        {
            return acceptor_;
        }
        endpoint local_endpoint() const
        {
            return acceptor_.local_endpoint();
        }
        inline static tcp::acceptor::reuse_address reuse_address_option() { return tcp::acceptor::reuse_address(true); }
    };

    struct UnixSocketAcceptor
    {
        using endpoint = stream_protocol::endpoint;
        stream_protocol::acceptor acceptor_;
        UnixSocketAcceptor(asio::io_context& io_context):
          acceptor_(io_context) {}

        int16_t port() const
        {
            return 0;
        }
        std::string address() const
        {
            return acceptor_.local_endpoint().path();
        }
        std::string url_display(bool) const
        {
            return acceptor_.local_endpoint().path();
        }
        stream_protocol::acceptor& raw_acceptor()
        {
            return acceptor_;
        }
        endpoint local_endpoint() const
        {
            return acceptor_.local_endpoint();
        }
        inline static stream_protocol::acceptor::reuse_address reuse_address_option()
        {
            // reuse addr must be false (https://github.com/chriskohlhoff/asio/issues/622)
            return stream_protocol::acceptor::reuse_address(false);
        }
    };
} // namespace crow