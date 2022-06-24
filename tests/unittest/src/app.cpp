#include "unittest.h"
#include "Middlewares.h"

struct OnlyMoveConstructor
{
    OnlyMoveConstructor(int) {}
    OnlyMoveConstructor(const OnlyMoveConstructor&) = delete;
    OnlyMoveConstructor(OnlyMoveConstructor&&) = default;
};

TEST_CASE("app_constructor")
{
    App<NullMiddleware, OnlyMoveConstructor, FirstMW<false>, SecondMW<false>>
      app1(OnlyMoveConstructor(1), SecondMW<false>{});
    App<NullMiddleware, OnlyMoveConstructor, FirstMW<false>, SecondMW<false>>
      app2(FirstMW<false>{}, OnlyMoveConstructor(1));
} // app_constructor

TEST_CASE("multi_server")
{
    static char buf[2048];
    SimpleApp app1, app2;
    CROW_ROUTE(app1, "/").methods("GET"_method,
                                  "POST"_method)([] {
        return "A";
    });
    CROW_ROUTE(app2, "/").methods("GET"_method,
                                  "POST"_method)([] {
        return "B";
    });

    auto _ = app1.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    auto _2 = app2.bindaddr(LOCALHOST_ADDRESS).port(45452).run_async();
    app1.wait_for_server_start();
    app2.wait_for_server_start();

    std::string sendmsg =
      "POST / HTTP/1.0\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n";
    {
        asio::io_service is;
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg));

        size_t recved = c.receive(asio::buffer(buf, 2048));
        CHECK('A' == buf[recved - 1]);
    }

    {
        asio::io_service is;
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45452));

        for (auto ch : sendmsg)
        {
            char buf[1] = {ch};
            c.send(asio::buffer(buf));
        }

        size_t recved = c.receive(asio::buffer(buf, 2048));
        CHECK('B' == buf[recved - 1]);
    }

    app1.stop();
    app2.stop();
} // multi_server

TEST_CASE("get_port")
{
    SimpleApp app;

    const std::uint16_t port = 12345;

    auto _ = app.port(port).run_async();

    app.wait_for_server_start();
    CHECK(app.port() == port);
    app.stop();

} // get_port

TEST_CASE("timeout")
{
    auto test_timeout = [](const std::uint8_t timeout) {
        static char buf[2048];

        SimpleApp app;

        CROW_ROUTE(app, "/")
        ([]() {
            return "hello";
        });

        auto _ = app.bindaddr(LOCALHOST_ADDRESS).timeout(timeout).port(45451).run_async();

        app.wait_for_server_start();
        asio::io_service is;
        std::string sendmsg = "GET /\r\n\r\n";
        future_status status;

        {
            asio::ip::tcp::socket c(is);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

            auto receive_future = async(launch::async, [&]() {
                asio::error_code ec;
                c.receive(asio::buffer(buf, 2048), 0, ec);
                return ec;
            });
            status = receive_future.wait_for(std::chrono::seconds(timeout - 1));
            CHECK(status == future_status::timeout);

            status = receive_future.wait_for(chrono::seconds(3));
            CHECK(status == future_status::ready);
            CHECK(receive_future.get() == asio::error::eof);

            c.close();
        }
        {
            asio::ip::tcp::socket c(is);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

            size_t received;
            auto receive_future = async(launch::async, [&]() {
                asio::error_code ec;
                received = c.receive(asio::buffer(buf, 2048), 0, ec);
                return ec;
            });
            status = receive_future.wait_for(std::chrono::seconds(timeout - 1));
            CHECK(status == future_status::timeout);

            c.send(asio::buffer(sendmsg));

            status = receive_future.wait_for(chrono::seconds(3));
            CHECK(status == future_status::ready);
            CHECK(!receive_future.get());
            CHECK("hello" == std::string(buf + received - 5, buf + received));

            c.close();
        }

        app.stop();
    };

    test_timeout(3);
    test_timeout(5);
} // timeout
