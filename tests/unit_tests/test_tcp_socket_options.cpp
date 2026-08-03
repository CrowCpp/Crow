#include "catch2/catch_all.hpp"

#include "crow.h"

#include <functional>
#include <string>
#include <vector>

using namespace std;
using namespace crow;

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
#else
using asio_error_code = asio::error_code;
#endif

namespace
{
    constexpr const char* kLocalhostAddress = "127.0.0.1";
    constexpr const char* kIpv6LocalhostAddress = "::1";

    class SocketTestHttpClient
    {
    public:
        SocketTestHttpClient(const std::string& address, uint16_t port):
          socket_(io_context_)
        {
            socket_.connect(asio::ip::tcp::endpoint(asio::ip::make_address(address), port));
        }

        void send(const std::string& msg)
        {
            socket_.send(asio::buffer(msg));
        }

        std::string receive()
        {
            char buffer[2048];
            const auto received = socket_.receive(asio::buffer(buffer, sizeof(buffer)));
            return std::string(buffer, received);
        }

        static std::string request(const std::string& address, uint16_t port, const std::string& request)
        {
            SocketTestHttpClient client(address, port);
            client.send(request);
            return client.receive();
        }

    private:
        asio::io_context io_context_;
        asio::ip::tcp::socket socket_;
    };

    bool is_tcp_nodelay_enabled_for_connection_after_apply(const crow::detail::socket::tcp_socket_options& options)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context,
                                         asio::ip::tcp::endpoint(asio::ip::make_address(kLocalhostAddress), 0));

        asio::ip::tcp::socket client_socket(io_context);
        client_socket.connect(acceptor.local_endpoint());

        asio::ip::tcp::socket server_socket(io_context);
        acceptor.accept(server_socket);

        crow::detail::socket::apply_tcp_socket_options(server_socket, options);

        asio::ip::tcp::no_delay no_delay;
        server_socket.get_option(no_delay);
        return no_delay.value();
    }

    bool is_tcp_keep_alive_enabled_for_connection_after_apply(const crow::detail::socket::tcp_socket_options& options)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context,
                                         asio::ip::tcp::endpoint(asio::ip::make_address(kLocalhostAddress), 0));

        asio::ip::tcp::socket client_socket(io_context);
        client_socket.connect(acceptor.local_endpoint());

        asio::ip::tcp::socket server_socket(io_context);
        acceptor.accept(server_socket);

        crow::detail::socket::apply_tcp_socket_options(server_socket, options);

        asio::socket_base::keep_alive keep_alive;
        server_socket.get_option(keep_alive);
        return keep_alive.value();
    }

    bool does_apply_keep_socket_default_for_nodelay(const crow::detail::socket::tcp_socket_options& options)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context,
                                         asio::ip::tcp::endpoint(asio::ip::make_address(kLocalhostAddress), 0));

        asio::ip::tcp::socket client_socket(io_context);
        client_socket.connect(acceptor.local_endpoint());

        asio::ip::tcp::socket server_socket(io_context);
        acceptor.accept(server_socket);

        asio::ip::tcp::no_delay before;
        server_socket.get_option(before);

        crow::detail::socket::apply_tcp_socket_options(server_socket, options);

        asio::ip::tcp::no_delay after;
        server_socket.get_option(after);
        return before.value() == after.value();
    }

    bool does_apply_keep_socket_default_for_keep_alive(const crow::detail::socket::tcp_socket_options& options)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context,
                                         asio::ip::tcp::endpoint(asio::ip::make_address(kLocalhostAddress), 0));

        asio::ip::tcp::socket client_socket(io_context);
        client_socket.connect(acceptor.local_endpoint());

        asio::ip::tcp::socket server_socket(io_context);
        acceptor.accept(server_socket);

        asio::socket_base::keep_alive before;
        server_socket.get_option(before);

        crow::detail::socket::apply_tcp_socket_options(server_socket, options);

        asio::socket_base::keep_alive after;
        server_socket.get_option(after);
        return before.value() == after.value();
    }

    bool is_acceptor_reuse_address_after_apply(const crow::detail::socket::tcp_socket_options& options, bool default_reuse_address)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context);
        acceptor.open(asio::ip::tcp::v4());

        const bool result = crow::detail::socket::apply_acceptor_socket_options(acceptor, options, default_reuse_address);
        REQUIRE(result);

        asio::socket_base::reuse_address reuse;
        acceptor.get_option(reuse);
        return reuse.value();
    }

    bool can_open_and_apply_v6_only(bool enabled)
    {
        asio::error_code ec;
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context);
        acceptor.open(asio::ip::tcp::v6(), ec);
        if (ec)
        {
            return false;
        }

        crow::detail::socket::tcp_socket_options options;
        options.set_v6_only(enabled);

        return crow::detail::socket::apply_acceptor_socket_options(acceptor, options, true);
    }

    bool is_acceptor_enable_connection_aborted_after_apply(const crow::detail::socket::tcp_socket_options& options, bool default_value)
    {
        asio::io_context io_context;
        asio::ip::tcp::acceptor acceptor(io_context);
        acceptor.open(asio::ip::tcp::v4());

        const bool result = crow::detail::socket::apply_acceptor_socket_options(acceptor, options, true);
        REQUIRE(result);

        asio::socket_base::enable_connection_aborted actual;
        actual = asio::socket_base::enable_connection_aborted(default_value);
        acceptor.get_option(actual);
        return actual.value();
    }

    std::string run_http_smoke(const std::function<void(crow::SimpleApp&)>& configure, const std::string& bind_address = kLocalhostAddress)
    {
        crow::SimpleApp app;

        CROW_ROUTE(app, "/")([] {
            return "ok";
        });

        app.validate();
        configure(app);

        auto _ = app.bindaddr(bind_address).port(0).run_async();
        app.wait_for_server_start();

        const auto response = SocketTestHttpClient::request(bind_address,
                                                            app.port(),
                                                            "GET / HTTP/1.0\r\n"
                                                            "Host: localhost\r\n"
                                                            "\r\n");

        app.stop();
        return response;
    }

    std::string run_websocket_upgrade_smoke(const std::function<void(crow::SimpleApp&)>& configure)
    {
        crow::SimpleApp app;

        CROW_WEBSOCKET_ROUTE(app, "/ws")
          .onopen([&](crow::websocket::connection& conn) {
              conn.send_text("Hello WebSocket");
          })
          .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool) {
              conn.send_text("Echo: " + data);
          })
          .onclose([&](crow::websocket::connection&, const std::string&, uint16_t) {});

        app.validate();
        configure(app);

        auto _ = app.bindaddr(kLocalhostAddress).port(0).run_async();
        app.wait_for_server_start();

        asio::io_context ic;
        asio::ip::tcp::socket socket(ic);
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address(kLocalhostAddress), app.port()));

        const std::string upgrade_request =
          "GET /ws HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Upgrade: websocket\r\n"
          "Connection: Upgrade\r\n"
          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
          "Sec-WebSocket-Version: 13\r\n"
          "\r\n";

        socket.send(asio::buffer(upgrade_request));

        std::vector<char> response_buffer(4096);
        const size_t bytes_received = socket.receive(asio::buffer(response_buffer));
        const std::string response(response_buffer.begin(), response_buffer.begin() + bytes_received);

        socket.close();
        app.stop();
        return response;
    }
} // namespace

TEST_CASE("tcp_socket_options_set_no_delay", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_no_delay(true);
    REQUIRE(options.no_delay.has_value());
    CHECK(options.no_delay->value() == true);

    options.set_no_delay(false);
    REQUIRE(options.no_delay.has_value());
    CHECK(options.no_delay->value() == false);
}

TEST_CASE("tcp_socket_options_set_keep_alive", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_keep_alive(true);
    REQUIRE(options.keep_alive.has_value());
    CHECK(options.keep_alive->value() == true);

    options.set_keep_alive(false);
    REQUIRE(options.keep_alive.has_value());
    CHECK(options.keep_alive->value() == false);
}

TEST_CASE("tcp_socket_options_set_receive_buffer_size", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_receive_buffer_size(65536);
    REQUIRE(options.receive_buffer_size.has_value());
    CHECK(options.receive_buffer_size->value() == 65536);
}

TEST_CASE("tcp_socket_options_set_send_buffer_size", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_send_buffer_size(32768);
    REQUIRE(options.send_buffer_size.has_value());
    CHECK(options.send_buffer_size->value() == 32768);
}

TEST_CASE("tcp_socket_options_set_linger", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_linger(true, 30);
    REQUIRE(options.linger.has_value());
    CHECK(options.linger->enabled() == true);
    CHECK(options.linger->timeout() == 30);

    options.set_linger(false, 0);
    REQUIRE(options.linger.has_value());
    CHECK(options.linger->enabled() == false);
}

TEST_CASE("tcp_socket_options_set_broadcast", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_broadcast(true);
    REQUIRE(options.broadcast.has_value());
    CHECK(options.broadcast->value() == true);

    options.set_broadcast(false);
    REQUIRE(options.broadcast.has_value());
    CHECK(options.broadcast->value() == false);
}

TEST_CASE("tcp_socket_options_set_debug", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_debug(true);
    REQUIRE(options.debug.has_value());
    CHECK(options.debug->value() == true);

    options.set_debug(false);
    REQUIRE(options.debug.has_value());
    CHECK(options.debug->value() == false);
}

TEST_CASE("tcp_socket_options_set_reuse_address", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_reuse_address(true);
    REQUIRE(options.reuse_address.has_value());
    CHECK(options.reuse_address->value() == true);

    options.set_reuse_address(false);
    REQUIRE(options.reuse_address.has_value());
    CHECK(options.reuse_address->value() == false);
}

TEST_CASE("tcp_socket_options_set_v6_only", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_v6_only(true);
    REQUIRE(options.v6_only.has_value());
    CHECK(options.v6_only->value() == true);

    options.set_v6_only(false);
    REQUIRE(options.v6_only.has_value());
    CHECK(options.v6_only->value() == false);
}

TEST_CASE("tcp_socket_options_set_enable_connection_aborted", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_enable_connection_aborted(true);
    REQUIRE(options.enable_connection_aborted.has_value());
    CHECK(options.enable_connection_aborted->value() == true);

    options.set_enable_connection_aborted(false);
    REQUIRE(options.enable_connection_aborted.has_value());
    CHECK(options.enable_connection_aborted->value() == false);
}

TEST_CASE("tcp_socket_options_set_listen_backlog", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    options.set_listen_backlog(512);
    REQUIRE(options.listen_backlog.has_value());
    CHECK(*options.listen_backlog == 512);

    options.set_listen_backlog(0);
    CHECK_FALSE(options.listen_backlog.has_value());

    options.set_listen_backlog(-10);
    CHECK_FALSE(options.listen_backlog.has_value());
}

TEST_CASE("tcp_socket_options_resolve_acceptor_backlog", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;

    CHECK(crow::detail::socket::resolve_acceptor_listen_backlog(options, 128) == 128);

    options.set_listen_backlog(256);
    CHECK(crow::detail::socket::resolve_acceptor_listen_backlog(options, 128) == 256);

    options.set_listen_backlog(0);
    CHECK(crow::detail::socket::resolve_acceptor_listen_backlog(options, 128) == 128);
}

TEST_CASE("socket_option_apply_tcp_nodelay", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options enable_options;
    enable_options.set_no_delay(true);
    CHECK(is_tcp_nodelay_enabled_for_connection_after_apply(enable_options) == true);

    crow::detail::socket::tcp_socket_options disable_options;
    disable_options.set_no_delay(false);
    CHECK(is_tcp_nodelay_enabled_for_connection_after_apply(disable_options) == false);
}

TEST_CASE("socket_option_apply_keep_alive", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options enable_options;
    enable_options.set_keep_alive(true);
    CHECK(is_tcp_keep_alive_enabled_for_connection_after_apply(enable_options) == true);

    crow::detail::socket::tcp_socket_options disable_options;
    disable_options.set_keep_alive(false);
    CHECK(is_tcp_keep_alive_enabled_for_connection_after_apply(disable_options) == false);
}

TEST_CASE("socket_option_apply_defaults_do_not_modify_bool_socket_options", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options defaults;
    CHECK(does_apply_keep_socket_default_for_nodelay(defaults));
    CHECK(does_apply_keep_socket_default_for_keep_alive(defaults));
}

TEST_CASE("acceptor_reuse_address_uses_default_when_not_configured", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options options;
    CHECK(is_acceptor_reuse_address_after_apply(options, true) == true);
    CHECK(is_acceptor_reuse_address_after_apply(options, false) == false);
}

TEST_CASE("acceptor_reuse_address_can_override_default", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options enable_options;
    enable_options.set_reuse_address(true);
    CHECK(is_acceptor_reuse_address_after_apply(enable_options, false) == true);

    crow::detail::socket::tcp_socket_options disable_options;
    disable_options.set_reuse_address(false);
    CHECK(is_acceptor_reuse_address_after_apply(disable_options, true) == false);
}

TEST_CASE("acceptor_v6_only_can_be_applied_when_ipv6_is_available", "[socket-options]")
{
    if (!can_open_and_apply_v6_only(true))
    {
        SKIP("IPv6 or v6_only option not available in this environment");
    }

    CHECK(can_open_and_apply_v6_only(true) == true);
    CHECK(can_open_and_apply_v6_only(false) == true);
}

TEST_CASE("acceptor_enable_connection_aborted_default_and_override", "[socket-options]")
{
    crow::detail::socket::tcp_socket_options defaults;
    CHECK(is_acceptor_enable_connection_aborted_after_apply(defaults, false) == false);

    crow::detail::socket::tcp_socket_options enable_options;
    enable_options.set_enable_connection_aborted(true);
    CHECK(is_acceptor_enable_connection_aborted_after_apply(enable_options, false) == true);

    crow::detail::socket::tcp_socket_options disable_options;
    disable_options.set_enable_connection_aborted(false);
    CHECK(is_acceptor_enable_connection_aborted_after_apply(disable_options, true) == false);
}

TEST_CASE("http_socket_api_defaults_and_setters", "[socket-options]")
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([] {
        return "ok";
    });

    app.validate();

    const auto& defaults = app.tcp_socket_options();
    CHECK_FALSE(defaults.no_delay.has_value());
    CHECK_FALSE(defaults.keep_alive.has_value());
    CHECK_FALSE(defaults.receive_buffer_size.has_value());
    CHECK_FALSE(defaults.send_buffer_size.has_value());
    CHECK_FALSE(defaults.linger.has_value());
    CHECK_FALSE(defaults.broadcast.has_value());
    CHECK_FALSE(defaults.debug.has_value());
    CHECK_FALSE(defaults.reuse_address.has_value());
    CHECK_FALSE(defaults.v6_only.has_value());
    CHECK_FALSE(defaults.enable_connection_aborted.has_value());
    CHECK_FALSE(defaults.listen_backlog.has_value());

    app.tcp_nodelay(true)
      .tcp_keep_alive(true)
      .tcp_receive_buffer_size(65536)
      .tcp_send_buffer_size(32768)
      .tcp_linger(true, 45)
      .tcp_broadcast(true)
      .tcp_debug(true)
      .tcp_reuse_address(true)
      .tcp_v6_only(true)
      .tcp_enable_connection_aborted(true)
      .tcp_listen_backlog(511);

    const auto& options = app.tcp_socket_options();
    REQUIRE(options.no_delay.has_value());
    CHECK(options.no_delay->value() == true);
    REQUIRE(options.keep_alive.has_value());
    CHECK(options.keep_alive->value() == true);
    REQUIRE(options.receive_buffer_size.has_value());
    CHECK(options.receive_buffer_size->value() == 65536);
    REQUIRE(options.send_buffer_size.has_value());
    CHECK(options.send_buffer_size->value() == 32768);
    REQUIRE(options.linger.has_value());
    CHECK(options.linger->enabled() == true);
    CHECK(options.linger->timeout() == 45);
    REQUIRE(options.broadcast.has_value());
    CHECK(options.broadcast->value() == true);
    REQUIRE(options.debug.has_value());
    CHECK(options.debug->value() == true);
    REQUIRE(options.reuse_address.has_value());
    CHECK(options.reuse_address->value() == true);
    REQUIRE(options.v6_only.has_value());
    CHECK(options.v6_only->value() == true);
    REQUIRE(options.enable_connection_aborted.has_value());
    CHECK(options.enable_connection_aborted->value() == true);
    REQUIRE(options.listen_backlog.has_value());
    CHECK(*options.listen_backlog == 511);

    app.tcp_nodelay(false)
      .tcp_keep_alive(false)
      .tcp_linger(false, 0)
      .tcp_broadcast(false)
      .tcp_debug(false)
      .tcp_reuse_address(false)
      .tcp_v6_only(false)
      .tcp_enable_connection_aborted(false)
      .tcp_listen_backlog(0);

    const auto& updated_options = app.tcp_socket_options();
    REQUIRE(updated_options.no_delay.has_value());
    CHECK(updated_options.no_delay->value() == false);
    REQUIRE(updated_options.keep_alive.has_value());
    CHECK(updated_options.keep_alive->value() == false);
    REQUIRE(updated_options.linger.has_value());
    CHECK(updated_options.linger->enabled() == false);
    REQUIRE(updated_options.broadcast.has_value());
    CHECK(updated_options.broadcast->value() == false);
    REQUIRE(updated_options.debug.has_value());
    CHECK(updated_options.debug->value() == false);
    REQUIRE(updated_options.reuse_address.has_value());
    CHECK(updated_options.reuse_address->value() == false);
    REQUIRE(updated_options.v6_only.has_value());
    CHECK(updated_options.v6_only->value() == false);
    REQUIRE(updated_options.enable_connection_aborted.has_value());
    CHECK(updated_options.enable_connection_aborted->value() == false);
    CHECK_FALSE(updated_options.listen_backlog.has_value());
}

TEST_CASE("websocket_socket_api_defaults_and_setters", "[socket-options]")
{
    crow::SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onopen([&](crow::websocket::connection&) {})
      .onmessage([&](crow::websocket::connection&, const std::string&, bool) {});

    app.validate();

    const auto& defaults = app.websocket_tcp_socket_options();
    CHECK_FALSE(defaults.no_delay.has_value());
    CHECK_FALSE(defaults.keep_alive.has_value());
    CHECK_FALSE(defaults.receive_buffer_size.has_value());
    CHECK_FALSE(defaults.send_buffer_size.has_value());
    CHECK_FALSE(defaults.linger.has_value());
    CHECK_FALSE(defaults.broadcast.has_value());
    CHECK_FALSE(defaults.debug.has_value());

    app.websocket_tcp_nodelay(true)
      .websocket_tcp_keep_alive(true)
      .websocket_tcp_receive_buffer_size(65536)
      .websocket_tcp_send_buffer_size(32768)
      .websocket_tcp_linger(true, 10)
      .websocket_tcp_broadcast(true)
      .websocket_tcp_debug(true);

    const auto& options = app.websocket_tcp_socket_options();
    REQUIRE(options.no_delay.has_value());
    CHECK(options.no_delay->value() == true);
    REQUIRE(options.keep_alive.has_value());
    CHECK(options.keep_alive->value() == true);
    REQUIRE(options.receive_buffer_size.has_value());
    CHECK(options.receive_buffer_size->value() == 65536);
    REQUIRE(options.send_buffer_size.has_value());
    CHECK(options.send_buffer_size->value() == 32768);
    REQUIRE(options.linger.has_value());
    CHECK(options.linger->enabled() == true);
    CHECK(options.linger->timeout() == 10);
    REQUIRE(options.broadcast.has_value());
    CHECK(options.broadcast->value() == true);
    REQUIRE(options.debug.has_value());
    CHECK(options.debug->value() == true);

    app.websocket_tcp_nodelay(false)
      .websocket_tcp_keep_alive(false)
      .websocket_tcp_linger(false, 0)
      .websocket_tcp_broadcast(false)
      .websocket_tcp_debug(false);

    const auto& updated_options = app.websocket_tcp_socket_options();
    REQUIRE(updated_options.no_delay.has_value());
    CHECK(updated_options.no_delay->value() == false);
    REQUIRE(updated_options.keep_alive.has_value());
    CHECK(updated_options.keep_alive->value() == false);
    REQUIRE(updated_options.linger.has_value());
    CHECK(updated_options.linger->enabled() == false);
    REQUIRE(updated_options.broadcast.has_value());
    CHECK(updated_options.broadcast->value() == false);
    REQUIRE(updated_options.debug.has_value());
    CHECK(updated_options.debug->value() == false);
}

TEST_CASE("tcp_nodelay_http_smoke_test", "[socket-options]")
{
    const auto response = run_http_smoke([](crow::SimpleApp& app) {
        app.tcp_nodelay(true);
    });

    CHECK(response.find("200 OK") != std::string::npos);
    CHECK(response.find("ok") != std::string::npos);
}

TEST_CASE("tcp_nodelay_websocket_smoke_test", "[socket-options]")
{
    const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
        app.websocket_tcp_nodelay(true);
    });

    CHECK(response.find("101") != std::string::npos);
    CHECK(response.find("Upgrade") != std::string::npos);
}

TEST_CASE("http_socket_options_full_smoke_enable_all_added_options", "[socket-options]")
{
    const auto response = run_http_smoke([](crow::SimpleApp& app) {
        app.tcp_nodelay(true)
          .tcp_keep_alive(true)
          .tcp_receive_buffer_size(65536)
          .tcp_send_buffer_size(32768)
          .tcp_linger(true, 20)
          .tcp_broadcast(true)
          .tcp_debug(true)
          .tcp_reuse_address(true)
          .tcp_enable_connection_aborted(true)
          .tcp_listen_backlog(511);
    });

    CHECK(response.find("200 OK") != std::string::npos);
    CHECK(response.find("ok") != std::string::npos);
}

TEST_CASE("http_socket_options_full_smoke_disable_bool_options", "[socket-options]")
{
    const auto response = run_http_smoke([](crow::SimpleApp& app) {
        app.tcp_nodelay(false)
          .tcp_keep_alive(false)
          .tcp_linger(false, 0)
          .tcp_broadcast(false)
          .tcp_debug(false)
          .tcp_reuse_address(false)
          .tcp_enable_connection_aborted(false)
          .tcp_listen_backlog(0);
    });

    CHECK(response.find("200 OK") != std::string::npos);
    CHECK(response.find("ok") != std::string::npos);
}

TEST_CASE("http_socket_options_smoke_v6_only", "[socket-options]")
{
    try
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_v6_only(true)
              .tcp_reuse_address(true)
              .tcp_nodelay(true);
        },
        kIpv6LocalhostAddress);

        CHECK(response.find("200 OK") != std::string::npos);
        CHECK(response.find("ok") != std::string::npos);
    }
    catch (...)
    {
        SKIP("IPv6 smoke test is not available in this environment");
    }
}

TEST_CASE("websocket_socket_options_full_smoke_enable_all_added_options", "[socket-options]")
{
    const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
        app.websocket_tcp_nodelay(true)
          .websocket_tcp_keep_alive(true)
          .websocket_tcp_receive_buffer_size(65536)
          .websocket_tcp_send_buffer_size(32768)
          .websocket_tcp_linger(true, 20)
          .websocket_tcp_broadcast(true)
          .websocket_tcp_debug(true);
    });

    CHECK(response.find("101") != std::string::npos);
    CHECK(response.find("Upgrade") != std::string::npos);
}

TEST_CASE("websocket_socket_options_full_smoke_disable_bool_options", "[socket-options]")
{
    const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
        app.websocket_tcp_nodelay(false)
          .websocket_tcp_keep_alive(false)
          .websocket_tcp_linger(false, 0)
          .websocket_tcp_broadcast(false)
          .websocket_tcp_debug(false);
    });

    CHECK(response.find("101") != std::string::npos);
    CHECK(response.find("Upgrade") != std::string::npos);
}

TEST_CASE("http_socket_options_per_option_smoke", "[socket-options]")
{
    SECTION("tcp_keep_alive enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_keep_alive(true);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_keep_alive disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_keep_alive(false);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_receive_buffer_size configured")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_receive_buffer_size(65536);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_send_buffer_size configured")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_send_buffer_size(65536);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_linger enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_linger(true, 10);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_linger disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_linger(false, 0);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_broadcast enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_broadcast(true);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_broadcast disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_broadcast(false);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_debug enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_debug(true);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_debug disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_debug(false);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_reuse_address enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_reuse_address(true);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_reuse_address disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_reuse_address(false);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_enable_connection_aborted enabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_enable_connection_aborted(true);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_enable_connection_aborted disabled")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_enable_connection_aborted(false);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_listen_backlog configured")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_listen_backlog(256);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }

    SECTION("tcp_listen_backlog reset to default")
    {
        const auto response = run_http_smoke([](crow::SimpleApp& app) {
            app.tcp_listen_backlog(0);
        });
        CHECK(response.find("200 OK") != std::string::npos);
    }
}

TEST_CASE("websocket_socket_options_per_option_smoke", "[socket-options]")
{
    SECTION("websocket_tcp_keep_alive enabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_keep_alive(true);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_keep_alive disabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_keep_alive(false);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_receive_buffer_size configured")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_receive_buffer_size(65536);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_send_buffer_size configured")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_send_buffer_size(65536);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_linger enabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_linger(true, 10);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_linger disabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_linger(false, 0);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_broadcast enabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_broadcast(true);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_broadcast disabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_broadcast(false);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_debug enabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_debug(true);
        });
        CHECK(response.find("101") != std::string::npos);
    }

    SECTION("websocket_tcp_debug disabled")
    {
        const auto response = run_websocket_upgrade_smoke([](crow::SimpleApp& app) {
            app.websocket_tcp_debug(false);
        });
        CHECK(response.find("101") != std::string::npos);
    }
}
