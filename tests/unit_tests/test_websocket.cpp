#include "catch2/catch_all.hpp"

#include "crow.h"

using namespace std;
using namespace crow;

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
using asio_error_code = asio::error_code;
#endif

#define LOCALHOST_ADDRESS "127.0.0.1"

TEST_CASE("websocket", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Host: localhost\r\n"
      "\r\n";

    static bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onaccept([&](const crow::request& req, void**) {
          CROW_LOG_INFO << "Accepted websocket with URL " << req.url;
          return true;
      })
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
          else if (!isbin && message.empty())
              conn.send_text("");
          else if (isbin && message == "Hello bin")
              conn.send_binary("Hello back bin");
      })
      .onclose([&](websocket::connection&, const std::string&, uint16_t) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45451));


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
    //----------Empty Text----------
    {
        std::fill_n(buf, 2048, 0);
        char text_message[2 + 0 + 1]("\x81\x00");

        c.send(asio::buffer(text_message, 2));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::string checkstring(std::string(buf).substr(0, 2));
        CHECK(checkstring == text_message);
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

TEST_CASE("websocket_close", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Host: localhost\r\n"
      "\r\n";

    static bool connected{false};
    websocket::connection* connection = nullptr;
    uint32_t close_calls = 0;
    uint16_t last_status_code = 0;
    CROW_LOG_INFO << "Setting up app!\n";
    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onaccept([&](const crow::request& req, void**) {
          CROW_LOG_INFO << "Accepted websocket with URL " << req.url;
          return true;
      })
      .onopen([&](websocket::connection& conn) {
          connected = true;
          connection = &conn;
          CROW_LOG_INFO << "Connected websocket and value is " << connected;
      })
      .onmessage([&](websocket::connection& conn, const std::string& message, bool) {
          CROW_LOG_INFO << "Message is \"" << message << '\"';
          if (message == "quit-default")
              conn.close();
          else if (message == "quit-custom")
              conn.close("custom", crow::websocket::StartStatusCodesForPrivateUse + 10u);
      })
      .onclose([&](websocket::connection& conn, const std::string&, uint16_t status_code) {
          // There should just be one connection
          CHECK(&conn == connection);
          CHECK_FALSE(conn.get_remote_ip().empty());
          ++close_calls;
          last_status_code = status_code;
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    CROW_LOG_WARNING << "Starting app!\n";
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45453).run_async();
    app.wait_for_server_start();
    CROW_LOG_WARNING << "App started!\n";
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45453));

    CROW_LOG_WARNING << "Connected!\n";

    char buf[2048];

    //----------Handshake----------
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(connected);
    }

    CHECK(close_calls == 0);

    SECTION("normal close from client")
    {
        std::fill_n(buf, 2048, 0);
        // Close message with, len = 2, status code = 1000
        char close_message[5]("\x88\x02\x03\xE8");
        c.send(asio::buffer(close_message, 4));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);
        CHECK((int)(unsigned char)buf[1] == 0x02);
        CHECK((int)(unsigned char)buf[2] == 0x03);
        CHECK((int)(unsigned char)buf[3] == 0xE8);

        CHECK(close_calls == 1);
        CHECK(last_status_code == 1000);
    }

    SECTION("empty close from client")
    {
        std::fill_n(buf, 2048, 0);
        // Close message with, len = 0, status code = N/A -> To application give no status code present
        char close_message[3]("\x88\x00");

        c.send(asio::buffer(close_message, 2));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);

        CHECK(close_calls == 1);
        CHECK(last_status_code == crow::websocket::NoStatusCodePresent);
    }

    SECTION("close with message from client")
    {
        std::fill_n(buf, 2048, 0);
        // Close message with, len = 2, status code = 1001
        char close_message[9]("\x88\x06\x03\xE9"
                              "fail");

        c.send(asio::buffer(close_message, 8));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);
        CHECK((int)(unsigned char)buf[1] == 0x06);
        CHECK((int)(unsigned char)buf[2] == 0x03);
        CHECK((int)(unsigned char)buf[3] == 0xE9);
        std::string checkstring(std::string(buf).substr(4, 4));
        CHECK(checkstring == "fail");

        CHECK(close_calls == 1);
        CHECK(last_status_code == 1001);
    }

    SECTION("normal close from server")
    {
        //----------Text----------
        std::fill_n(buf, 2048, 0);
        char text_message[2 + 12 + 1]("\x81\x0C"
                                      "quit-default");

        c.send(asio::buffer(text_message, 14));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);
        // length is message + 2 for status code
        CHECK((int)(unsigned char)buf[1] == 0x6);

        uint16_t expected_code = websocket::NormalClosure;
        CHECK((int)(unsigned char)buf[2] == expected_code >> 8);
        CHECK((int)(unsigned char)buf[3] == (expected_code & 0xff));
        std::string checkstring(std::string(buf).substr(4, 4));
        CHECK(checkstring == "quit");

        CHECK(close_calls == 0);

        // Reply with client close
        char client_close_response[9]("\x88\x06\x0\x0quit");
        client_close_response[2] = buf[2];
        client_close_response[3] = buf[3];

        c.send(asio::buffer(client_close_response, 8));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(close_calls == 1);
        CHECK(last_status_code == expected_code);
    }

    SECTION("custom close from server")
    {
        //----------Text----------
        std::fill_n(buf, 2048, 0);
        char text_message[2 + 11 + 1]("\x81\x0B"
                                      "quit-custom");

        c.send(asio::buffer(text_message, 13));
        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK((int)(unsigned char)buf[0] == 0x88);
        // length is message + 2 for status code
        CHECK((int)(unsigned char)buf[1] == 0x8);
        uint16_t expected_code = 4010;
        CHECK((int)(unsigned char)buf[2] == expected_code >> 8);
        CHECK((int)(unsigned char)buf[3] == (expected_code & 0xff));
        std::string checkstring(std::string(buf).substr(4, 6));
        CHECK(checkstring == "custom");

        CHECK(close_calls == 0);

        // Reply with client close
        char client_close_response[11]("\x88\x08\x0\x0"
                                       "custom");
        client_close_response[2] = buf[2];
        client_close_response[3] = buf[3];

        c.send(asio::buffer(client_close_response, 10));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(close_calls == 1);
        CHECK(last_status_code == expected_code);
    }

    CROW_LOG_WARNING << "Stopping app!\n";
    app.stop();
}

TEST_CASE("websocket_missing_host", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";

    static bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onaccept([&](const crow::request& req, void**) {
          CROW_LOG_INFO << "Accepted websocket with URL " << req.url;
          return true;
      })
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
      .onclose([&](websocket::connection&, const std::string&, uint16_t) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45471).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45471));


    char buf[2048];

    // Handshake should fail
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(!connected);
    }

    app.stop();
} // websocket

TEST_CASE("websocket_max_payload", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Host: localhost\r\n"
      "\r\n";

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
      .onclose([&](websocket::connection&, const std::string&, uint16_t) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.websocket_max_payload(3).bindaddr(LOCALHOST_ADDRESS).port(45461).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45461));


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

    asio_error_code ec;
    c.lowest_layer().shutdown(asio::socket_base::shutdown_type::shutdown_both, ec);

    app.stop();
} // websocket_max_payload

TEST_CASE("websocket_subprotocols", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Protocol: myprotocol\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Host: localhost\r\n"
      "\r\n";

    static websocket::connection* connection = nullptr;
    static bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .subprotocols({"anotherprotocol", "myprotocol"})
      .onaccept([&](const crow::request& req, void**) {
          CROW_LOG_INFO << "Accepted websocket with URL " << req.url;
          return true;
      })
      .onopen([&](websocket::connection& con) {
          connected = true;
          connection = &con;
          CROW_LOG_INFO << "Connected websocket and subprotocol is " << con.get_subprotocol();
      })
      .onclose([&](websocket::connection&, const std::string&, uint16_t) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45451));


    char buf[2048];

    //----------Handshake----------
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(connected);
        CHECK(connection->get_subprotocol() == "myprotocol");
    }

    app.stop();
}

TEST_CASE("mirror_websocket_subprotocols", "[websocket]")
{
    static std::string http_message = 
      "GET /ws HTTP/1.1\r\n"
      "Connection: keep-alive, Upgrade\r\n"
      "upgrade: websocket\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Protocol: protocol1, protocol2\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Host: localhost\r\n"
      "\r\n";

    websocket::connection* connection = nullptr;
    bool connected{false};

    SimpleApp app;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .mirrorprotocols()
      .onaccept([&](const crow::request& req, void**) {
          CROW_LOG_INFO << "Accepted websocket with URL " << req.url;
          return true;
      })
      .onopen([&](websocket::connection& con) {
          connected = true;
          connection = &con;
          CROW_LOG_INFO << "Connected websocket and subprotocol is " << con.get_subprotocol();
      })
      .onclose([&](websocket::connection&, const std::string&, uint16_t) {
          CROW_LOG_INFO << "Closing websocket";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    asio::ip::tcp::socket c(ic);
    c.connect(asio::ip::tcp::endpoint(
      asio::ip::make_address(LOCALHOST_ADDRESS), 45451));


    char buf[2048];

    //----------Handshake----------
    {
        std::fill_n(buf, 2048, 0);
        c.send(asio::buffer(http_message));

        c.receive(asio::buffer(buf, 2048));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(connected);
        CHECK(connection->get_subprotocol() == "protocol1, protocol2");
    }

    app.stop();
}