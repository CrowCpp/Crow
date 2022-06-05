#define CATCH_CONFIG_MAIN
#define CROW_LOG_LEVEL 0

#include <thread>

#include "../catch.hpp"
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

    boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23);

    boost::asio::io_service is;
    {
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> c(is, ctx);
        c.lowest_layer().connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(LOCALHOST_ADDRESS), 45460));

        c.handshake(boost::asio::ssl::stream_base::client);
        c.write_some(boost::asio::buffer(sendmsg));

        size_t x = 0;
        size_t y = 0;
        long z = 0;

        while (x < 121)
        {
            y = c.read_some(boost::asio::buffer(buf, 2048));
            x += y;
            buf[y] = '\0';
        }

        std::string to_test(buf);

        if ((z = to_test.length() - 24) >= 0)
        {

            CHECK(std::string("Hello world, I'm keycrt.") == to_test.substr(z));
        }
        else
        {
            CHECK(std::string("Hello world, I'm keycrt.").substr((z * -1)) == to_test);
        }

        boost::system::error_code ec;
        c.lowest_layer().shutdown(boost::asio::socket_base::shutdown_type::shutdown_both, ec);
    }

    /*
    boost::asio::io_service is2;
    {
        std::cout << "started second one" << std::endl;

      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> c(is2, ctx);
      c.lowest_layer().connect(boost::asio::ip::tcp::endpoint(
          boost::asio::ip::address::from_string(LOCALHOST_ADDRESS), 45461));

      c.handshake(boost::asio::ssl::stream_base::client);

      c.write_some(boost::asio::buffer(sendmsg));

      size_t sz = c.read_some(boost::asio::buffer(buf2, 2048));
      CHECK("Hello world, I'm pem." == std::string(buf2).substr((sz - 21)));
    }
*/
    app.stop();
    //app2.stop();

    std::system("rm test.crt test.key" /*test.pem*/);
}
