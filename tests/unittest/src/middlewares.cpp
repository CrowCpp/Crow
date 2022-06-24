#include "unittest.h"
#include "crow/middlewares/cookie_parser.h"
#include "crow/middlewares/cors.h"
#include "Middlewares.h"

TEST_CASE("middleware_simple")
{
    App<NullMiddleware, NullSimpleMiddleware> app;
    decltype(app)::server_t server(&app, LOCALHOST_ADDRESS, 45451);
    CROW_ROUTE(app, "/")
    ([&](const crow::request& req) {
        app.get_context<NullMiddleware>(req);
        app.get_context<NullSimpleMiddleware>(req);
        return "";
    });
}

TEST_CASE("middleware_context")
{
    static char buf[2048];
    // SecondMW depends on FirstMW (it uses all_ctx.get<FirstMW>)
    // so it leads to compile error if we remove FirstMW from definition
    // App<IntSettingMiddleware, SecondMW> app;
    // or change the order of FirstMW and SecondMW
    // App<IntSettingMiddleware, SecondMW, FirstMW> app;

    App<IntSettingMiddleware, FirstMW<false>, SecondMW<false>, ThirdMW<false>> app;

    int x{};
    CROW_ROUTE(app, "/")
    ([&](const request& req) {
        {
            auto& ctx = app.get_context<IntSettingMiddleware>(req);
            x = ctx.val;
        }
        {
            auto& ctx = app.get_context<FirstMW<false>>(req);
            ctx.v.push_back("handle");
        }

        return "";
    });
    CROW_ROUTE(app, "/break")
    ([&](const request& req) {
        {
            auto& ctx = app.get_context<FirstMW<false>>(req);
            ctx.v.push_back("handle");
        }

        return "";
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg = "GET /\r\n\r\n";
    asio::io_service is;
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg));

        c.receive(asio::buffer(buf, 2048));
        c.close();
    }
    {
        auto& out = test_middleware_context_vector;
        CHECK(1 == x);
        CHECK(7 == out.size());
        CHECK("1 before" == out[0]);
        CHECK("2 before" == out[1]);
        CHECK("3 before" == out[2]);
        CHECK("handle" == out[3]);
        CHECK("3 after" == out[4]);
        CHECK("2 after" == out[5]);
        CHECK("1 after" == out[6]);
    }
    std::string sendmsg2 = "GET /break\r\n\r\n";
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg2));

        c.receive(asio::buffer(buf, 2048));
        c.close();
    }
    {
        auto& out = test_middleware_context_vector;
        CHECK(4 == out.size());
        CHECK("1 before" == out[0]);
        CHECK("2 before" == out[1]);
        CHECK("2 after" == out[2]);
        CHECK("1 after" == out[3]);
    }
    app.stop();
} // middleware_context

TEST_CASE("local_middleware")
{
    static char buf[2048];

    App<LocalSecretMiddleware> app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "works!";
    });

    CROW_ROUTE(app, "/secret")
      .middlewares<decltype(app), LocalSecretMiddleware>()([]() {
          return "works!";
      });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_service is;

    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer("GET /\r\n\r\n"));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(std::string(buf).find("200") != std::string::npos);
    }

    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer("GET /secret\r\n\r\n"));
        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(std::string(buf).find("403") != std::string::npos);
    }

    app.stop();
} // local_middleware

TEST_CASE("middleware_blueprint")
{
    // Same logic as middleware_context, but middleware is added with blueprints
    static char buf[2048];

    App<FirstMW<true>, SecondMW<true>, ThirdMW<true>> app;

    Blueprint bp1("a", "c1", "c1");
    bp1.CROW_MIDDLEWARES(app, FirstMW<true>);

    Blueprint bp2("b", "c2", "c2");
    bp2.CROW_MIDDLEWARES(app, SecondMW<true>);

    Blueprint bp3("c", "c3", "c3");

    CROW_BP_ROUTE(bp2, "/")
      .CROW_MIDDLEWARES(app, ThirdMW<true>)([&app](const crow::request& req) {
          {
              auto& ctx = app.get_context<FirstMW<true>>(req);
              ctx.v.push_back("handle");
          }
          return "";
      });

    CROW_BP_ROUTE(bp3, "/break")
      .CROW_MIDDLEWARES(app, SecondMW<true>, ThirdMW<true>)([&app](const crow::request& req) {
          {
              auto& ctx = app.get_context<FirstMW<true>>(req);
              ctx.v.push_back("handle");
          }
          return "";
      });

    bp1.register_blueprint(bp3);
    bp1.register_blueprint(bp2);
    app.register_blueprint(bp1);

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    asio::io_service is;
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer("GET /a/b/\r\n\r\n"));

        c.receive(asio::buffer(buf, 2048));
        c.close();
    }
    {
        auto& out = test_middleware_context_vector;
        CHECK(7 == out.size());
        CHECK("1 before" == out[0]);
        CHECK("2 before" == out[1]);
        CHECK("3 before" == out[2]);
        CHECK("handle" == out[3]);
        CHECK("3 after" == out[4]);
        CHECK("2 after" == out[5]);
        CHECK("1 after" == out[6]);
    }
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer("GET /a/c/break\r\n\r\n"));

        c.receive(asio::buffer(buf, 2048));
        c.close();
    }
    {
        auto& out = test_middleware_context_vector;
        CHECK(4 == out.size());
        CHECK("1 before" == out[0]);
        CHECK("2 before" == out[1]);
        CHECK("2 after" == out[2]);
        CHECK("1 after" == out[3]);
    }

    app.stop();
} // middleware_blueprint


TEST_CASE("middleware_cookieparser_parse")
{
    static char buf[2048];

    App<CookieParser> app;

    std::string value1;
    std::string value2;
    std::string value3;
    std::string value4;

    CROW_ROUTE(app, "/")
    ([&](const request& req) {
        {
            auto& ctx = app.get_context<CookieParser>(req);
            value1 = ctx.get_cookie("key1");
            value2 = ctx.get_cookie("key2");
            value3 = ctx.get_cookie("key3");
            value4 = ctx.get_cookie("key4");
        }

        return "";
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg =
      "GET /\r\nCookie: key1=value1; key2=\"val=ue2\"; key3=\"val\"ue3\"; "
      "key4=\"val\"ue4\"\r\n\r\n";
    asio::io_service is;
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg));

        c.receive(asio::buffer(buf, 2048));
        c.close();
    }
    {
        CHECK("value1" == value1);
        CHECK("val=ue2" == value2);
        CHECK("val\"ue3" == value3);
        CHECK("val\"ue4" == value4);
    }
    app.stop();
} // middleware_cookieparser_parse


TEST_CASE("middleware_cookieparser_format")
{
    using Cookie = CookieParser::Cookie;

    auto valid = [](const std::string& s, int parts) {
        return std::count(s.begin(), s.end(), ';') == parts - 1;
    };

    // basic
    {
        auto c = Cookie("key", "value");
        auto s = c.dump();
        CHECK(valid(s, 1));
        CHECK(s == "key=value");
    }
    // max-age + domain
    {
        auto c = Cookie("key", "value")
                   .max_age(123)
                   .domain("example.com");
        auto s = c.dump();
        CHECK(valid(s, 3));
        CHECK(s.find("key=value") != std::string::npos);
        CHECK(s.find("Max-Age=123") != std::string::npos);
        CHECK(s.find("Domain=example.com") != std::string::npos);
    }
    // samesite + secure
    {
        auto c = Cookie("key", "value")
                   .secure()
                   .same_site(Cookie::SameSitePolicy::None);
        auto s = c.dump();
        CHECK(valid(s, 3));
        CHECK(s.find("Secure") != std::string::npos);
        CHECK(s.find("SameSite=None") != std::string::npos);
    }
    // expires
    {
        std::time_t tp;
        std::time(&tp);
        std::tm* tm = std::gmtime(&tp);
        std::istringstream ss("2000-11-01 23:59:59");
        ss >> std::get_time(tm, "%Y-%m-%d %H:%M:%S");
        std::mktime(tm);

        auto c = Cookie("key", "value")
                   .expires(*tm);
        auto s = c.dump();
        CHECK(valid(s, 2));
        CHECK(s.find("Expires=Wed, 01 Nov 2000 23:59:59 GMT") != std::string::npos);
    }
} // middleware_cookieparser_format

TEST_CASE("middleware_cors")
{
    static char buf[5012];

    App<crow::CORSHandler> app;

    auto& cors = app.get_middleware<crow::CORSHandler>();
    // clang-format off
    cors
      .prefix("/origin")
        .origin("test.test")
      .prefix("/nocors")
        .ignore();
    // clang-format on

    CROW_ROUTE(app, "/")
    ([&](const request&) {
        return "-";
    });

    CROW_ROUTE(app, "/origin")
    ([&](const request&) {
        return "-";
    });

    CROW_ROUTE(app, "/nocors/path")
    ([&](const request&) {
        return "-";
    });

    auto _ = async(launch::async,
                   [&] {
                       app.bindaddr(LOCALHOST_ADDRESS).port(45451).run();
                   });

    app.wait_for_server_start();
    asio::io_service is;

    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer("GET /\r\n\r\n"));

        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(std::string(buf).find("Access-Control-Allow-Origin: *") != std::string::npos);
    }

    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer("GET /origin\r\n\r\n"));

        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(std::string(buf).find("Access-Control-Allow-Origin: test.test") != std::string::npos);
    }

    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer("GET /nocors/path\r\n\r\n"));

        c.receive(asio::buffer(buf, 2048));
        c.close();

        CHECK(std::string(buf).find("Access-Control-Allow-Origin:") == std::string::npos);
    }

    app.stop();
} // middleware_cors
