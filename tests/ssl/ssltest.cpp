#define CROW_LOG_LEVEL 0

#include <thread>

#include "catch2/catch_all.hpp"
#include "crow.h"

#define LOCALHOST_ADDRESS "127.0.0.1"

// TODO(EDev): SSL test with .pem file
TEST_CASE("SSL")
{
    static char buf[2048];
    //static char buf2[2048];

    std::system("openssl req -newkey rsa:4096 -x509 -sha256 -days 365 -nodes -out test.crt -keyout test.key -subj '/CN=127.0.0.1'");
    //std::system("cat test.key > test.pem");
    //std::system("cat test.crt >> test.pem");

    crow::SimpleApp app;
    //crow::SimpleApp app2;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello world, I'm keycrt.";
    });
    /*
    CROW_ROUTE(app2, "/")
    ([]() {
        return "Hello world, I'm pem.";
    });
*/
    auto _ = async(std::launch::async, [&] {
        app.bindaddr(LOCALHOST_ADDRESS).port(45460).ssl_file("test.crt", "test.key").run();
    });
    //auto _1 = async(std::launch::async,[&] { app2.bindaddr(LOCALHOST_ADDRESS).port(45461).ssl_file("test.pem").run(); });

    app.wait_for_server_start();
    //app2.wait_for_server_start();

    std::string sendmsg = "GET /\r\n\r\n";

    asio::ssl::context ctx(asio::ssl::context::sslv23);

    asio::io_context ioc;
    {
        asio::ssl::stream<asio::ip::tcp::socket> c(ioc, ctx);
        c.lowest_layer().connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), 45460));

        c.handshake(asio::ssl::stream_base::client);
        c.write_some(asio::buffer(sendmsg));

        std::string http_response;
        size_t y = 0;

        asio::error_code ec{};
        while (!ec) {
            y = c.read_some(asio::buffer(buf, 2048),ec);
            http_response.append(buf,y);
        }
        auto start_body =  http_response.find("\r\n\r\n");
        CHECK(start_body!=std::string::npos);

        CHECK(std::string("Hello world, I'm keycrt.") == http_response.substr(start_body+4));

        c.lowest_layer().shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);
    }

    /*
    asio::io_service is2;
    {
        std::cout << "started second one" << std::endl;

      asio::ssl::stream<asio::ip::tcp::socket> c(is2, ctx);
      c.lowest_layer().connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45461));

      c.handshake(asio::ssl::stream_base::client);

      c.write_some(asio::buffer(sendmsg));

      size_t sz = c.read_some(asio::buffer(buf2, 2048));
      CHECK("Hello world, I'm pem." == std::string(buf2).substr((sz - 21)));
    }
*/
    app.stop();
    //app2.stop();

    std::system("rm test.crt test.key" /*test.pem*/);
}
