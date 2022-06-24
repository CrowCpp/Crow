#include "unittest.h"

TEST_CASE("websocket")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

    static bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws").onopen([&](websocket::connection&) {
                                        connected = true;
                                        CROW_LOG_INFO << "Connected websocket and value is " << connected;
                                    })
      .onmessage([&](websocket::connection& conn, const std::string& message, bool isbin) {
          CROW_LOG_INFO << "Message is \"" << message << '\"';
          if (!isbin && message == "PINGME")
              conn.send_ping("");
          else if (!isbin && message == "Hello")
              conn.send_text("Hello back");
          else if (isbin && message == "Hello bin")
              conn.send_binary("Hello back bin");
      })
      .onclose([&](websocket::connection&, const std::string&) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_service is;

    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));


    char buf[2048];

    //----------Handshake----------
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(connected);
    }
    //----------Pong----------
    {
        std::fill_n(buf, 2048, 0);
        char ping_message[2]("\x89");

        c.send(asio::buffer(ping_message, 2));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x8A);
    }
    //----------Ping----------
    {
        std::fill_n(buf, 2048, 0);
        char not_ping_message[2 + 6 + 1]("\x81\x06"
                                         "PINGME");

        c.send(asio::buffer(not_ping_message, 8));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x89);
    }
    //----------Text----------
    {
        std::fill_n(buf, 2048, 0);
        char text_message[2 + 5 + 1]("\x81\x05"
                                     "Hello");

        c.send(asio::buffer(text_message, 7));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring(std::string(buf).substr(0, 12));
        CHECK(checkstring == "\x81\x0AHello back");
    }
    //----------Binary----------
    {
        std::fill_n(buf, 2048, 0);
        char bin_message[2 + 9 + 1]("\x82\x09"
                                    "Hello bin");

        c.send(asio::buffer(bin_message, 11));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring2(std::string(buf).substr(0, 16));
        CHECK(checkstring2 == "\x82\x0EHello back bin");
    }
    //----------Masked Text----------
    {
        std::fill_n(buf, 2048, 0);
        char text_masked_message[2 + 4 + 5 + 1]("\x81\x85"
                                                "\x67\xc6\x69\x73"
                                                "\x2f\xa3\x05\x1f\x08");

        c.send(asio::buffer(text_masked_message, 11));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring3(std::string(buf).substr(0, 12));
        CHECK(checkstring3 == "\x81\x0AHello back");
    }
    //----------Masked Binary----------
    {
        std::fill_n(buf, 2048, 0);
        char bin_masked_message[2 + 4 + 9 + 1]("\x82\x89"
                                               "\x67\xc6\x69\x73"
                                               "\x2f\xa3\x05\x1f\x08\xe6\x0b\x1a\x09");

        c.send(asio::buffer(bin_masked_message, 15));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring4(std::string(buf).substr(0, 16));
        CHECK(checkstring4 == "\x82\x0EHello back bin");
    }
    //----------16bit Length Text----------
    {
        std::fill_n(buf, 2048, 0);
        char b16_text_message[2 + 2 + 5 + 1]("\x81\x7E"
                                             "\x00\x05"
                                             "Hello");

        c.send(asio::buffer(b16_text_message, 9));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring(std::string(buf).substr(0, 12));
        CHECK(checkstring == "\x81\x0AHello back");
    }
    //----------64bit Length Text----------
    {
        std::fill_n(buf, 2048, 0);
        char b64text_message[2 + 8 + 5 + 1]("\x81\x7F"
                                            "\x00\x00\x00\x00\x00\x00\x00\x05"
                                            "Hello");

        c.send(asio::buffer(b64text_message, 15));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring(std::string(buf).substr(0, 12));
        CHECK(checkstring == "\x81\x0AHello back");
    }
    //----------Close----------
    {
        std::fill_n(buf, 2048, 0);
        char close_message[10]("\x88"); //I do not know why, but the websocket code does not read this unless it's longer than 4 or so bytes

        c.send(asio::buffer(close_message, 10));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);
    }

    app.stop();
} // websocket

TEST_CASE("websocket_max_payload")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

    static bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onopen([&](websocket::connection&) {
          connected = true;
          CROW_LOG_INFO << "Connected websocket and value is " << connected;
      })
      .onmessage([&](websocket::connection& conn, const std::string& message, bool isbin) {
          CROW_LOG_INFO << "Message is \"" << message << '\"';
          if (!isbin && message == "PINGME")
              conn.send_ping("");
          else if (!isbin && message == "Hello")
              conn.send_text("Hello back");
          else if (isbin && message == "Hello bin")
              conn.send_binary("Hello back bin");
      })
      .onclose([&](websocket::connection&, const std::string&) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.websocket_max_payload(3).bindaddr(LOCALHOST_ADDRESS).port(45461).run_async();
    app.wait_for_server_start();
    asio::io_service is;

    asio::ip::tcp::socket c(is);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::address::from_string(LOCALHOST_ADDRESS), 45461));


    char buf[2048];

    //----------Handshake----------
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(connected);
    }
    //----------Text----------
    {
        std::fill_n(buf, 2048, 0);
        char text_message[2 + 5 + 1]("\x81\x05"
                                     "Hello");

        c.send(asio::buffer(text_message, 7));
        try
        {
            c.receive(asio::buffer(buf, 2048));
            FAIL_CHECK();
        }
        catch (std::exception& e)
        {
            CROW_LOG_DEBUG << "websocket_max_payload test passed due to the exception: " << e.what();
        }
    }

    asio::error_code ec;
    c.lowest_layer().shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);

    app.stop();
} // websocket_max_payload
