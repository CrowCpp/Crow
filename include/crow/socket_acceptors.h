#pragma once
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>

namespace crow
{
    using tcp = asio::ip::tcp;
    using stream_protocol = asio::local::stream_protocol;

    struct TCPAcceptor
    {
        using endpoint = tcp::endpoint;
        tcp::acceptor acceptor_;
        TCPAcceptor(asio::io_service& io_service, const endpoint& endpoint_):
          acceptor_(io_service, endpoint_) {}

        int16_t port() const
        {
            return acceptor_.local_endpoint().port();
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
    };

    struct UnixSocketAcceptor
    {
        using endpoint = stream_protocol::endpoint;
        stream_protocol::acceptor acceptor_;
        UnixSocketAcceptor(asio::io_service& io_service, const endpoint& endpoint_):
          acceptor_(io_service, endpoint_, false) {}
        // reuse addr must be false (https://github.com/chriskohlhoff/asio/issues/622)

        int16_t port() const
        {
            return 0;
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
    };
} // namespace crow