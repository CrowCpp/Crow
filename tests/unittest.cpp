#define CROW_ENABLE_DEBUG
#define CROW_LOG_LEVEL 0
#include <sys/stat.h>

#include <exception>
#include <iostream>
#include <vector>
#include <thread>
#include <type_traits>
#include <regex>

#include "catch2/catch_all.hpp"
#include "crow.h"
#include "crow/middlewares/cookie_parser.h"
#include "crow/middlewares/cors.h"
#include "crow/middlewares/session.h"

using namespace std;
using namespace crow;

#ifdef CROW_USE_BOOST
namespace asio = boost::asio;
using asio_error_code = boost::system::error_code;
#else
using asio_error_code = asio::error_code;
#endif

#define LOCALHOST_ADDRESS "127.0.0.1"

/** simple http client class for making client requests */
class HttpClient
{
private:
    asio::io_context ic{};
    asio::ip::tcp::socket c;

public:
    /** construct an instance by address and port */
    HttpClient(std::string const& address, uint16_t port):
      c(ic)
    {
        c.connect(asio::ip::tcp::endpoint( asio::ip::make_address(address),
                                           port));
    }

    /** sends a request string through the socket */
    void send(const std::string& msg)
    {
        c.send(asio::buffer(msg));
    }

    /** sends a request string through the socket */
    void send(const char* const msg, size_t msg_size)
    {
        c.send(asio::buffer(msg, msg_size));
    }


    /** method shall be called after sending a request with send
     * @returns the received response string */
    std::string receive()
    {
        char buf[2048];
        auto received = c.receive(asio::buffer(buf, sizeof(buf)));
        std::string rval(buf, received);
        return rval;
    }

    /** static method for making a request
     * @returns the received response string */
    static std::string request(const std::string& address,
                               uint16_t port,
                               const std::string& sendmsg)
    {
        HttpClient c(address, port);
        c.send(sendmsg);
        return c.receive();
    }
};

TEST_CASE("Rule")
{
    TaggedRule<> r("/http/");
    r.name("abc");

    // empty handler - fail to validate
    try
    {
        r.validate();
        FAIL_CHECK("empty handler should fail to validate");
    }
    catch (runtime_error& e)
    {
    }

    int x = 0;

    // registering handler
    r([&x] {
        x = 1;
        return "";
    });

    r.validate();

    response res;
    request req;

    // executing handler
    CHECK(0 == x);
    r.handle(req, res, routing_params());
    CHECK(1 == x);

    // registering handler with request argument
    r([&x](const crow::request&) {
        x = 2;
        return "";
    });

    r.validate();

    // executing handler
    CHECK(1 == x);
    r.handle(req, res, routing_params());
    CHECK(2 == x);
} // Rule

TEST_CASE("ParameterTagging")
{
    static_assert(black_magic::is_valid("<int><int><int>"), "valid url");
    static_assert(!black_magic::is_valid("<int><int<<int>"), "invalid url");
    static_assert(!black_magic::is_valid("nt>"), "invalid url");
    CHECK(1 == black_magic::get_parameter_tag("<int>"));
    CHECK(2 == black_magic::get_parameter_tag("<uint>"));
    CHECK(3 == black_magic::get_parameter_tag("<float>"));
    CHECK(3 == black_magic::get_parameter_tag("<double>"));
    CHECK(4 == black_magic::get_parameter_tag("<str>"));
    CHECK(4 == black_magic::get_parameter_tag("<string>"));
    CHECK(5 == black_magic::get_parameter_tag("<path>"));
    CHECK(6 * 6 + 6 + 1 == black_magic::get_parameter_tag("<int><int><int>"));
    CHECK(6 * 6 + 6 + 2 == black_magic::get_parameter_tag("<uint><int><int>"));
    CHECK(6 * 6 + 6 * 3 + 2 ==
          black_magic::get_parameter_tag("<uint><double><int>"));

    // url definition parsed in compile time, build into *one number*, and given
    // to template argument
    static_assert(
      std::is_same<black_magic::S<uint64_t, double, int64_t>,
                   black_magic::arguments<6 * 6 + 6 * 3 + 2>::type>::value,
      "tag to type container");
} // ParameterTagging

TEST_CASE("PathRouting")
{
    SimpleApp app;

    CROW_ROUTE(app, "/file")
    ([] {
        return "file";
    });

    CROW_ROUTE(app, "/path/")
    ([] {
        return "path";
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/file";

        app.handle_full(req, res);

        CHECK(200 == res.code);
    }
    {
        request req;
        response res;

        req.url = "/file/";

        app.handle_full(req, res);
        CHECK(404 == res.code);
    }
    {
        request req;
        response res;

        req.url = "/path";

        app.handle_full(req, res);
        CHECK(404 != res.code);
    }
    {
        request req;
        response res;

        req.url = "/path/";

        app.handle_full(req, res);
        CHECK(200 == res.code);
    }
} // PathRouting

TEST_CASE("InvalidPathRouting")
{
    SimpleApp app;

    CROW_ROUTE(app, "invalid_route")
    ([] {
        return "should not arrive here";
    });

    try
    {
        app.validate();
        FAIL_CHECK();
    }
    catch (std::exception& e)
    {
        auto expected_exception_text = "Internal error: Routes must start with a '/'";
        CHECK(strcmp(expected_exception_text, e.what()) == 0);
    }
} // InvalidPathRouting

TEST_CASE("RoutingTest")
{
    SimpleApp app;
    int A{};
    uint32_t B{};
    double C{};
    string D{};
    string E{};

    CROW_ROUTE(app, "/0/<uint>")
    ([&](uint32_t b) {
        B = b;
        return "OK";
    });

    CROW_ROUTE(app, "/1/<int>/<uint>")
    ([&](int a, uint32_t b) {
        A = a;
        B = b;
        return "OK";
    });

    CROW_ROUTE(app, "/4/<int>/<uint>/<double>/<string>")
    ([&](int a, uint32_t b, double c, string d) {
        A = a;
        B = b;
        C = c;
        D = d;
        return "OK";
    });

    CROW_ROUTE(app, "/5/<int>/<uint>/<double>/<string>/<path>")
    ([&](int a, uint32_t b, double c, string d, string e) {
        A = a;
        B = b;
        C = c;
        D = d;
        E = e;
        return "OK";
    });

    app.validate();
    // app.debug_print();
    {
        request req;
        response res;

        req.url = "/-1";

        app.handle_full(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/0/1001999";

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(1001999 == B);
    }

    {
        request req;
        response res;

        req.url = "/1/-100/1999";

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(-100 == A);
        CHECK(1999 == B);
    }
    {
        request req;
        response res;

        req.url = "/4/5000/3/-2.71828/hellhere";
        req.add_header("TestHeader", "Value");

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(5000 == A);
        CHECK(3 == B);
        REQUIRE_THAT(-2.71828, Catch::Matchers::WithinAbs(C, 1e-9));
        CHECK("hellhere" == D);
    }
    {
        request req;
        response res;

        req.url = "/5/-5/999/3.141592/hello_there/a/b/c/d";
        req.add_header("TestHeader", "Value");

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(-5 == A);
        CHECK(999 == B);
        REQUIRE_THAT(3.141592, Catch::Matchers::WithinAbs(C, 1e-9));
        CHECK("hello_there" == D);
        CHECK("a/b/c/d" == E);
    }
} // RoutingTest

TEST_CASE("routing_params")
{
    routing_params rp;
    rp.int_params.push_back(1);
    rp.int_params.push_back(5);
    rp.uint_params.push_back(2);
    rp.double_params.push_back(3);
    rp.string_params.push_back("hello");
    CHECK(1 == rp.get<int64_t>(0));
    CHECK(5 == rp.get<int64_t>(1));
    CHECK(2 == rp.get<uint64_t>(0));
    REQUIRE_THAT(3, Catch::Matchers::WithinAbs(rp.get<double>(0), 1e-9));
    CHECK("hello" == rp.get<string>(0));
} // routing_params

TEST_CASE("handler_with_response")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([](const crow::request&, crow::response&) {});
} // handler_with_response

TEST_CASE("http_method")
{
    SimpleApp app;

    CROW_ROUTE(app, "/").methods("POST"_method,
                                 "GET"_method)([](const request& req) {
        if (req.method == "GET"_method)
            return "2";
        else
            return "1";
    });

    CROW_ROUTE(app, "/head_only")
      .methods("HEAD"_method)([](const request& /*req*/) {
          return response{202};
      });
    CROW_ROUTE(app, "/get_only")
      .methods("GET"_method)([](const request& /*req*/) {
          return "get";
      });
    CROW_ROUTE(app, "/post_only")
      .methods("POST"_method)([](const request& /*req*/) {
          return "post";
      });
    CROW_ROUTE(app, "/patch_only")
      .methods("PATCH"_method)([](const request& /*req*/) {
          return "patch";
      });
    CROW_ROUTE(app, "/purge_only")
      .methods("PURGE"_method)([](const request& /*req*/) {
          return "purge";
      });

    app.validate();
    app.debug_print();

    // cannot have multiple handlers for the same url
    // CROW_ROUTE(app, "/")
    //.methods("GET"_method)
    //([]{ return "2"; });

    {
        request req;
        response res;

        req.url = "/";
        app.handle_full(req, res);

        CHECK("2" == res.body);
    }
    {
        request req;
        response res;

        req.url = "/";
        req.method = "POST"_method;
        app.handle_full(req, res);

        CHECK("1" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/head_only";
        req.method = "HEAD"_method;
        app.handle_full(req, res);

        CHECK(202 == res.code);
        CHECK("" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        app.handle_full(req, res);

        CHECK("get" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/patch_only";
        req.method = "PATCH"_method;
        app.handle_full(req, res);

        CHECK("patch" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/purge_only";
        req.method = "PURGE"_method;
        app.handle_full(req, res);

        CHECK("purge" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "POST"_method;
        app.handle_full(req, res);

        CHECK("get" != res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "POST"_method;
        app.handle_full(req, res);

        CHECK(405 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "HEAD"_method;
        app.handle_full(req, res);

        CHECK(200 == res.code);
        CHECK("" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/";
        req.method = "OPTIONS"_method;
        app.handle_full(req, res);

#ifdef CROW_RETURNS_OK_ON_HTTP_OPTIONS_REQUEST
        CHECK(200 == res.code);
#else
        CHECK(204 == res.code);
#endif
        CHECK("OPTIONS, HEAD, GET, POST" == res.get_header_value("Allow"));
    }

    {
        request req;
        response res;

        req.url = "/does_not_exist";
        req.method = "OPTIONS"_method;
        app.handle_full(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/*";
        req.method = "OPTIONS"_method;
        app.handle_full(req, res);

#ifdef CROW_RETURNS_OK_ON_HTTP_OPTIONS_REQUEST
        CHECK(200 == res.code);
#else
        CHECK(204 == res.code);
#endif
        CHECK("OPTIONS, HEAD, GET, POST, PATCH, PURGE" == res.get_header_value("Allow"));
    }

    {
        request req;
        response res;

        req.url = "/head_only";
        req.method = "OPTIONS"_method;
        app.handle_full(req, res);

#ifdef CROW_RETURNS_OK_ON_HTTP_OPTIONS_REQUEST
        CHECK(200 == res.code);
#else
        CHECK(204 == res.code);
#endif
        CHECK("OPTIONS, HEAD" == res.get_header_value("Allow"));
    }
} // http_method

TEST_CASE("validate can be called multiple times")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([]() {
        return "1";
    });
    app.validate();
    app.validate();

    CROW_ROUTE(app, "/test")
    ([]() {
        return "1";
    });
    app.validate();

    try
    {
        CROW_ROUTE(app, "/")
        ([]() {
            return "1";
        });
        app.validate();
        FAIL_CHECK();
    }
    catch (std::exception& e)
    {
        CROW_LOG_DEBUG << e.what();
    }
} // validate can be called multiple times

TEST_CASE("server_handling_error_request")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    // Server<SimpleApp> server(&app, LOCALHOST_ADDRESS, 45451);
    // auto _ = async(launch::async, [&]{server.run();});
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();

    try
    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451, "POX");
        FAIL_CHECK();
    }
    catch (std::exception& e)
    {
        CROW_LOG_DEBUG << e.what();
    }
    app.stop();
} // server_handling_error_request

TEST_CASE("server_invalid_ip_address")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    auto _ = app.bindaddr("192.").port(45451).run_async();
    auto state = app.wait_for_server_start();

    // we should run into a timeout as the server will not started
    CHECK(state==cv_status::timeout);
} // server_invalid_ip_address


TEST_CASE("server_dynamic_port_allocation")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(0).run_async();
    app.wait_for_server_start();
    asio::io_context ic;
    {
        asio::ip::tcp::socket c(ic);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::make_address(LOCALHOST_ADDRESS), app.port()));
    }
    app.stop();
} // server_dynamic_port_allocation

TEST_CASE("server_handling_error_request_http_version")
{
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    {
        try
        {
            auto resp = HttpClient::request(LOCALHOST_ADDRESS,
                                                   45451,
                                                   "POST /\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n");
            FAIL_CHECK();
        }
        catch (std::exception& e)
        {
            CROW_LOG_DEBUG << e.what();
        }
    }
    app.stop();
} // server_handling_error_request_http_version

TEST_CASE("multi_server")
{
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
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "POST / HTTP/1.0\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n");
        CHECK('A' == resp.at(resp.length() - 1));
    }

    {
        HttpClient c(LOCALHOST_ADDRESS, 45452);
        for (auto ch : sendmsg)
        {
            char buff[1] = {ch};
            c.send(buff, 1);
        }

        auto resp = c.receive();
        CHECK('B' == resp.at(resp.length() - 1));
    }

    app1.stop();
    app2.stop();
} // multi_server


TEST_CASE("undefined_status_code")
{
    SimpleApp app;
    CROW_ROUTE(app, "/get123")
    ([] {
        //this status does not exist statusCodes map defined in include/crow/http_connection.h
        const int undefinedStatusCode = 123;
        return response(undefinedStatusCode, "this should return 500");
    });

    CROW_ROUTE(app, "/get200")
    ([] {
        return response(200, "ok");
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45471).run_async();
    app.wait_for_server_start();

    asio::io_context ic;
    auto sendRequestAndGetStatusCode = [&](const std::string& route) -> unsigned {
        asio::ip::tcp::socket socket(ic);
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), app.port()));

        asio::streambuf request;
        std::ostream request_stream(&request);
        request_stream << "GET " << route << " HTTP/1.0\r\n";
        request_stream << "Host: " << LOCALHOST_ADDRESS << "\r\n";
        request_stream << "Accept: */*\r\n";
        request_stream << "Connection: close\r\n\r\n";

        // Send the request.
        asio::write(socket, request);

        asio::streambuf response;
        asio::read_until(socket, response, "\r\n");

        std::istream response_stream(&response);
        std::string http_version;
        response_stream >> http_version;
        unsigned status_code = 0;
        response_stream >> status_code;

        return status_code;
    };

    unsigned statusCode = sendRequestAndGetStatusCode("/get200");
    CHECK(statusCode == 200);

    statusCode = sendRequestAndGetStatusCode("/get123");
    CHECK(statusCode == 500);

    app.stop();
} // undefined_status_code

TEST_CASE("black_magic")
{
    using namespace black_magic;
    static_assert(
      std::is_same<void, last_element_type<int, char, void>::type>::value,
      "last_element_type");
    static_assert(std::is_same<char, pop_back<int, char, void>::rebind<
                                       last_element_type>::type>::value,
                  "pop_back");
    static_assert(
      std::is_same<int, pop_back<int, char, void>::rebind<pop_back>::rebind<
                          last_element_type>::type>::value,
      "pop_back");
} // black_magic

struct NullMiddleware
{
    struct context
    {};

    template<typename AllContext>
    void before_handle(request&, response&, context&, AllContext&)
    {}

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext&)
    {}
};

struct NullSimpleMiddleware
{
    struct context
    {};

    void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}

    void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}
};

TEST_CASE("middleware_simple")
{
    App<NullMiddleware, NullSimpleMiddleware> app;
    TCPAcceptor::endpoint endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451);
    decltype(app)::server_t server(&app, endpoint);

    CROW_ROUTE(app, "/")
    ([&](const crow::request& req) {
        app.get_context<NullMiddleware>(req);
        app.get_context<NullSimpleMiddleware>(req);
        return "";
    });
}

struct IntSettingMiddleware
{
    struct context
    {
        int val;
    };

    template<typename AllContext>
    void before_handle(request&, response&, context& ctx, AllContext&)
    {
        ctx.val = 1;
    }

    template<typename AllContext>
    void after_handle(request&, response&, context& ctx, AllContext&)
    {
        ctx.val = 2;
    }
};

std::vector<std::string> test_middleware_context_vector;

struct empty_type
{};

template<bool Local>
struct FirstMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {
        std::vector<string> v;
    };

    void before_handle(request& /*req*/, response& /*res*/, context& ctx)
    {
        ctx.v.push_back("1 before");
    }

    void after_handle(request& /*req*/, response& /*res*/, context& ctx)
    {
        ctx.v.push_back("1 after");
        test_middleware_context_vector = ctx.v;
    }
};

template<bool Local>
struct SecondMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {};
    template<typename AllContext>
    void before_handle(request& req, response& res, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("2 before");
        if (req.url.find("/break") != std::string::npos) res.end();
    }

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("2 after");
    }
};

template<bool Local>
struct ThirdMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {};
    template<typename AllContext>
    void before_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("3 before");
    }

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("3 after");
    }
};

TEST_CASE("middleware_context")
{
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
    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /\r\n\r\n");
    }
    {
        auto& out = test_middleware_context_vector;
        CHECK(1 == x);
        bool cond = 7 == out.size();
        CHECK(cond);
        if (cond)
        {
            CHECK("1 before" == out[0]);
            CHECK("2 before" == out[1]);
            CHECK("3 before" == out[2]);
            CHECK("handle" == out[3]);
            CHECK("3 after" == out[4]);
            CHECK("2 after" == out[5]);
            CHECK("1 after" == out[6]);
        }
    }
    std::string sendmsg2 = "GET /break\r\n\r\n";
    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        sendmsg2);
    }
    {
        auto& out = test_middleware_context_vector;
        bool cond = 4 == out.size();
        CHECK(cond);
        if (cond)
        {
            CHECK("1 before" == out[0]);
            CHECK("2 before" == out[1]);
            CHECK("2 after" == out[2]);
            CHECK("1 after" == out[3]);
        }
    }
    app.stop();
} // middleware_context

struct LocalSecretMiddleware : crow::ILocalMiddleware
{
    struct context
    {};

    void before_handle(request& /*req*/, response& res, context& /*ctx*/)
    {
        res.code = 403;
        res.end();
    }

    void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/)
    {}
};

TEST_CASE("local_middleware")
{

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

    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /\r\n\r\n");

        CHECK(resp.find("200") != std::string::npos);
    }

    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /secret\r\n\r\n");

        CHECK(resp.find("403") != std::string::npos);
    }

    app.stop();
} // local_middleware

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

TEST_CASE("middleware_blueprint")
{
    // Same logic as middleware_context, but middleware is added with blueprints
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

    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /a/b/\r\n\r\n");
    }
    {
        auto& out = test_middleware_context_vector;
        bool cond = 7 == out.size();
        CHECK(cond);
        if (cond)
        {
            CHECK("1 before" == out[0]);
            CHECK("2 before" == out[1]);
            CHECK("3 before" == out[2]);
            CHECK("handle" == out[3]);
            CHECK("3 after" == out[4]);
            CHECK("2 after" == out[5]);
            CHECK("1 after" == out[6]);
        }
    }
    {
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /a/c/break\r\n\r\n");
    }
    {
        auto& out = test_middleware_context_vector;
        bool cond = 4 == out.size();
        CHECK(cond);
        if (cond)
        {
            CHECK("1 before" == out[0]);
            CHECK("2 before" == out[1]);
            CHECK("2 after" == out[2]);
            CHECK("1 after" == out[3]);
        }
    }

    app.stop();
} // middleware_blueprint


TEST_CASE("middleware_cookieparser_parse")
{
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
    auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                    "GET /\r\nCookie: key1=value1; key2=\"val=ue2\"; key3=\"val\"ue3\"; "
                                    "key4=\"val\"ue4\"\r\n\r\n");
    CHECK("value1" == value1);
    CHECK("val=ue2" == value2);
    CHECK("val\"ue3" == value3);
    CHECK("val\"ue4" == value4);
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
    // prototype
    {
        auto c = Cookie("key");
        c.value("value");
        auto s = c.dump();
        CHECK(valid(s, 1));
        CHECK(s == "key=value");
    }
} // middleware_cookieparser_format

TEST_CASE("middleware_cors")
{
    App<crow::CORSHandler> app;

    auto& cors = app.get_middleware<crow::CORSHandler>();
    // clang-format off
    cors
      .prefix("/origin")
        .origin("test.test")
      .prefix("/auth-origin")
        .allow_credentials()
      .prefix("/expose")
        .expose("exposed-header")
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

    CROW_ROUTE(app, "/auth-origin")
    ([&](const request&) {
        return "-";
    });

    CROW_ROUTE(app, "/auth-origin").methods(crow::HTTPMethod::Post)([&](const request&) {
        return "-";
    });

    CROW_ROUTE(app, "/expose")
    ([&](const request&) {
        return "-";
    });

    CROW_ROUTE(app, "/nocors/path")
    ([&](const request&) {
        return "-";
    });

    const auto port = 33333;
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(port).run_async();
    app.wait_for_server_start();
    auto resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                                    "OPTIONS / HTTP/1.1\r\n\r\n");

    CHECK(resp.find("Access-Control-Allow-Origin: *") != std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "GET /\r\n\r\n");
    CHECK(resp.find("Access-Control-Allow-Origin: *") != std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "GET /origin\r\n\r\n");
    CHECK(resp.find("Access-Control-Allow-Origin: test.test") != std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "GET /auth-origin\r\nOrigin: test-client\r\n\r\n");
    CHECK(resp.find("Access-Control-Allow-Origin: test-client") != std::string::npos);
    CHECK(resp.find("Access-Control-Allow-Credentials: true") != std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "OPTIONS /auth-origin / HTTP/1.1 \r\n\r\n");
    CHECK(resp.find("Access-Control-Allow-Origin: *") != std::string::npos);
    CHECK(resp.find("Access-Control-Allow-Credentials: true") == std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "GET /expose\r\n\r\n");
    CHECK(resp.find("Access-Control-Expose-Headers: exposed-header") != std::string::npos);

    resp = HttpClient::request(LOCALHOST_ADDRESS, port,
                               "GET /nocors/path\r\n\r\n");

    CHECK(resp.find("Access-Control-Allow-Origin:") == std::string::npos);

    app.stop();
} // middleware_cors

TEST_CASE("middleware_session")
{
    static char buf[5012];

    using Session = SessionMiddleware<InMemoryStore>;

    App<crow::CookieParser, Session> app{
      Session{InMemoryStore{}}};

    CROW_ROUTE(app, "/get")
    ([&](const request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");
        return session.string(key);
    });

    CROW_ROUTE(app, "/set")
    ([&](const request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");
        auto value = req.url_params.get("value");
        session.set(key, value);
        return "ok";
    });

    CROW_ROUTE(app, "/count")
    ([&](const request& req) {
        auto& session = app.get_context<Session>(req);
        session.apply("counter", [](int v) {
            return v + 2;
        });
        return session.string("counter");
    });

    CROW_ROUTE(app, "/lock")
    ([&](const request& req) {
        auto& session = app.get_context<Session>(req);
        session.mutex().lock();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        session.mutex().unlock();
        return "OK";
    });

    CROW_ROUTE(app, "/check_lock")
    ([&](const request& req) {
        auto& session = app.get_context<Session>(req);
        if (session.mutex().try_lock())
            return "LOCKED";
        else
        {
            session.mutex().unlock();
            return "FAILED";
        };
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();

    app.wait_for_server_start();
    asio::io_context ic;

    auto make_request = [&](const std::string& rq) {
        asio::ip::tcp::socket c(ic);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(rq));
        c.receive(asio::buffer(buf, 2048));
        c.close();
        return std::string(buf);
    };

    std::string cookie = "Cookie: session=";

    // test = works
    {
        auto res = make_request(
          "GET /set?key=test&value=works\r\n" + cookie + "\r\n\r\n");

        const std::regex cookiev_regex("Cookie:\\ssession=(.*?);", std::regex::icase);
        auto istart = std::sregex_token_iterator(res.begin(), res.end(), cookiev_regex, 1);
        auto iend = std::sregex_token_iterator();

        CHECK(istart != iend);
        cookie.append(istart->str());
        cookie.push_back(';');
    }

    // check test = works
    {
        auto res = make_request("GET /get?key=test\r\n" + cookie + "\r\n\r\n");
        CHECK(res.find("works") != std::string::npos);
    }

    // check counter
    {
        for (int i = 1; i < 5; i++)
        {
            auto res = make_request("GET /count\r\n" + cookie + "\r\n\r\n");
            CHECK(res.find(std::to_string(2 * i)) != std::string::npos);
        }
    }

    // lock
    {
        asio::ip::tcp::socket c_lock(ic);
        c_lock.connect(asio::ip::tcp::endpoint(
          asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
        c_lock.send(asio::buffer("GET /lock\r\n" + cookie + "\r\n\r\n"));

        auto res = make_request("GET /check_lock\r\n" + cookie + "\r\n\r\n");
        CHECK(res.find("LOCKED") != std::string::npos);

        c_lock.close();
    }


    app.stop();
} // middleware_session


TEST_CASE("bug_quick_repeated_request")
{
    SimpleApp app;
    std::uint8_t explicitTimeout = 200;
    app.timeout(explicitTimeout);

    CROW_ROUTE(app, "/")
    ([&] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return "hello";
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

    auto start = std::chrono::high_resolution_clock::now();

    asio::io_context ic;
    {
        std::vector<std::future<void>> v;
        for (int i = 0; i < 5; i++)
        {
            v.push_back(async(launch::async, [&] {
                HttpClient c(LOCALHOST_ADDRESS, 45451);

                for (int j = 0; j < 5; j++)
                {
                    c.send(sendmsg);

                    auto resp = c.receive();
                    CHECK("hello" == resp.substr(resp.length() - 5));
                }
            }));
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::seconds testDuration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    CHECK(testDuration.count() < explicitTimeout);

    app.stop();
} // bug_quick_repeated_request

TEST_CASE("simple_url_params")
{
    SimpleApp app;

    query_string last_url_params;

    CROW_ROUTE(app, "/params")
    ([&last_url_params](const crow::request& req) {
        last_url_params = std::move(req.url_params);
        return "OK";
    });

    /// params?h=1&foo=bar&lol&count[]=1&count[]=4&pew=5.2

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    asio::io_context ic;

    // check empty params
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params\r\n\r\n");

    stringstream ss;
    ss << last_url_params;

    CHECK("[  ]" == ss.str());

    // check single presence
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?foobar\r\n\r\n");
    CHECK(last_url_params.get("missing") == nullptr);
    CHECK(last_url_params.get("foobar") != nullptr);
    CHECK(last_url_params.get_list("missing").empty());

    // check multiple presence
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?foo&bar&baz\r\n\r\n");

    CHECK(last_url_params.get("missing") == nullptr);
    CHECK(last_url_params.get("foo") != nullptr);
    CHECK(last_url_params.get("bar") != nullptr);
    CHECK(last_url_params.get("baz") != nullptr);

    // check single value
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?hello=world\r\n\r\n");
    CHECK(string(last_url_params.get("hello")) == "world");

    // check multiple value
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?hello=world&left=right&up=down\r\n\r\n");
    query_string mutable_params(last_url_params);

    CHECK(string(mutable_params.get("hello")) == "world");
    CHECK(string(mutable_params.get("left")) == "right");
    CHECK(string(mutable_params.get("up")) == "down");

    std::string z = mutable_params.pop("left");
    CHECK(z == "right");
    CHECK(mutable_params.get("left") == nullptr);

    // check multiple value, multiple types
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?int=100&double=123.45&boolean=1\r\n\r\n");

    CHECK(utility::lexical_cast<int>(last_url_params.get("int")) == 100);
    REQUIRE_THAT(123.45, Catch::Matchers::WithinAbs(utility::lexical_cast<double>(last_url_params.get("double")), 1e-9));
    CHECK(utility::lexical_cast<bool>(last_url_params.get("boolean")));

    // check single array value
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?tmnt[]=leonardo\r\n\r\n");
    CHECK(last_url_params.get("tmnt") == nullptr);
    CHECK(last_url_params.get_list("tmnt").size() == 1);
    CHECK(string(last_url_params.get_list("tmnt")[0]) == "leonardo");

    // check multiple array value
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?tmnt[]=leonardo&tmnt[]=donatello&tmnt[]=raphael\r\n\r\n");

    CHECK(last_url_params.get_list("tmnt").size() == 3);
    CHECK(string(last_url_params.get_list("tmnt")[0]) == "leonardo");
    CHECK(string(last_url_params.get_list("tmnt")[1]) == "donatello");
    CHECK(string(last_url_params.get_list("tmnt")[2]) == "raphael");
    CHECK(last_url_params.pop_list("tmnt").size() == 3);
    CHECK(last_url_params.get_list("tmnt").size() == 0);

    // check dictionary value
    HttpClient::request(LOCALHOST_ADDRESS, 45451,
                        "GET /params?kees[one]=vee1&kees[two]=vee2&kees[three]=vee3\r\n\r\n");
    CHECK(last_url_params.get_dict("kees").size() == 3);
    CHECK(string(last_url_params.get_dict("kees")["one"]) == "vee1");
    CHECK(string(last_url_params.get_dict("kees")["two"]) == "vee2");
    CHECK(string(last_url_params.get_dict("kees")["three"]) == "vee3");
    CHECK(last_url_params.pop_dict("kees").size() == 3);
    CHECK(last_url_params.get_dict("kees").size() == 0);

    app.stop();
} // simple_url_params

TEST_CASE("route_dynamic")
{
    SimpleApp app;
    int x = 1;
    app.route_dynamic("/")([&] {
        x = 2;
        return "";
    });

    app.route_dynamic("/set4")([&](const request&) {
        x = 4;
        return "";
    });
    app.route_dynamic("/set5")([&](const request&, response& res) {
        x = 5;
        res.end();
    });

    app.route_dynamic("/set_int/<int>")([&](int y) {
        x = y;
        return "";
    });

    try
    {
        app.route_dynamic("/invalid_test/<double>/<path>")([]() {
            return "";
        });
        FAIL_CHECK();
    }
    catch (std::exception&)
    {}

    // app is in an invalid state when route_dynamic throws an exception.
    try
    {
        app.validate();
        FAIL_CHECK();
    }
    catch (std::exception&)
    {}

    {
        request req;
        response res;
        req.url = "/";
        app.handle_full(req, res);
        CHECK(x == 2);
    }
    {
        request req;
        response res;
        req.url = "/set_int/42";
        app.handle_full(req, res);
        CHECK(x == 42);
    }
    {
        request req;
        response res;
        req.url = "/set5";
        app.handle_full(req, res);
        CHECK(x == 5);
    }
    {
        request req;
        response res;
        req.url = "/set4";
        app.handle_full(req, res);
        CHECK(x == 4);
    }
} // route_dynamic

TEST_CASE("multipart")
{
    //
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"hello\"
    //
    //world
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"world\"
    //
    //hello
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"multiline\"
    //
    //text
    //text
    //text
    //--CROW-BOUNDARY--
    //

    std::string test_string = "--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"hello\"\r\n\r\nworld\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"world\"\r\n\r\nhello\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"multiline\"\r\n\r\ntext\ntext\ntext\r\n--CROW-BOUNDARY--\r\n";

    SimpleApp app;

    CROW_ROUTE(app, "/multipart")
    ([](const crow::request& req, crow::response& res) {
        multipart::message msg(req);
        res.body = msg.dump();
        res.end();
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=CROW-BOUNDARY");
        req.body = test_string;

        app.handle_full(req, res);

        CHECK(test_string == res.body);


        multipart::message msg(req);
        CHECK("hello" == msg.get_part_by_name("world").body);
        CHECK("form-data" == msg.get_part_by_name("hello").get_header_object("Content-Disposition").value);
    }

    // Check with `"` encapsulating the boundary (.NET clients do this)
    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=\"CROW-BOUNDARY\"");
        req.body = test_string;

        app.handle_full(req, res);

        CHECK(test_string == res.body);
    }

    //Test against empty boundary
    {
        request req;
        response res;
        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=");
        req.body = test_string;

        app.handle_full(req, res);

        CHECK(res.code == 400);
        CHECK(res.body == "Empty boundary in multipart message");

    }

    //Boundary that differs from actual boundary
    {
        const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist(0, sizeof(alphabet) - 2);
        std::uniform_int_distribution<std::mt19937::result_type> boundary_sizes(1, 50);
        std::array<std::string, 100> test_boundaries;

        for (auto& boundary : test_boundaries)
        {
            const size_t boundary_size = boundary_sizes(rng);
            boundary.reserve(boundary_size);
            for (size_t idx = 0; idx < boundary_size; idx++)
                boundary.push_back(alphabet[dist(rng)]);
        }

        for (const auto& boundary : test_boundaries)
        {
            request req;
            response res;

            req.url = "/multipart";
            req.add_header("Content-Type", "multipart/form-data; boundary=" + boundary);
            req.body = test_string;

            app.handle_full(req, res);

            CHECK(res.code == 400);
            CHECK(res.body == "Unable to find delimiter in multipart message. Probably ill-formed body");
        }
    }
} // multipart

TEST_CASE("multipart_view")
{
    //
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"hello\"
    //
    //world
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"world\"
    //
    //hello
    //--CROW-BOUNDARY
    //Content-Disposition: form-data; name=\"multiline\"
    //
    //text
    //text
    //text
    //--CROW-BOUNDARY--
    //

    std::string test_string = "--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"hello\"\r\n\r\nworld\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"world\"\r\n\r\nhello\r\n--CROW-BOUNDARY\r\nContent-Disposition: form-data; name=\"multiline\"\r\n\r\ntext\ntext\ntext\r\n--CROW-BOUNDARY--\r\n";

    SimpleApp app;

    CROW_ROUTE(app, "/multipart")
    ([](const crow::request& req, crow::response& res) {
        multipart::message_view msg(req);
        res.body = msg.dump();
        res.end();
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=CROW-BOUNDARY");
        req.body = test_string;

        app.handle_full(req, res);

        CHECK(test_string == res.body);


        multipart::message_view msg(req);
        CHECK("hello" == msg.get_part_by_name("world").body);
        CHECK("form-data" == msg.get_part_by_name("hello").get_header_object("Content-Disposition").value);
    }

    // Check with `"` encapsulating the boundary (.NET clients do this)
    {
        request req;
        response res;

        req.url = "/multipart";
        req.add_header("Content-Type", "multipart/form-data; boundary=\"CROW-BOUNDARY\"");
        req.body = test_string;

        app.handle_full(req, res);

        CHECK(test_string == res.body);
    }
} // multipart_view

TEST_CASE("send_file")
{

    struct stat statbuf_cat;
    stat("tests/img/cat.jpg", &statbuf_cat);
    struct stat statbuf_badext;
    stat("tests/img/filewith.badext", &statbuf_badext);

    SimpleApp app;

    CROW_ROUTE(app, "/jpg")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/cat.jpg");
        res.end();
    });

    CROW_ROUTE(app, "/jpg2")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info(
          "tests/img/cat2.jpg"); // This file is nonexistent on purpose
        res.end();
    });

    CROW_ROUTE(app, "/jpg3")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/cat.jpg", "application/octet-stream"); // Set Content-Type explicitly
        res.end();
    });

    CROW_ROUTE(app, "/filewith.badext")
    ([](const crow::request&, crow::response& res) {
        res.set_static_file_info("tests/img/filewith.badext");
        res.end();
    });

    app.validate();

    //File not found check
    {
        request req;
        response res;

        req.url = "/jpg2";

        app.handle_full(req, res);


        CHECK(404 == res.code);
    }

    //Headers check
    {
        request req;
        response res;

        req.url = "/jpg";
        req.http_ver_major = 1;

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(res.headers.count("Content-Type"));
        if (res.headers.count("Content-Type"))
            CHECK("image/jpeg" == res.headers.find("Content-Type")->second);

        CHECK(res.headers.count("Content-Length"));
        if (res.headers.count("Content-Length"))
            CHECK(to_string(statbuf_cat.st_size) == res.headers.find("Content-Length")->second);
    }

    // Explicit Content-Type:
    {
        request req;
        response res;

        req.url = "/jpg3";
        req.http_ver_major = 1;

        app.handle_full(req, res);

        CHECK(200 == res.code);

        CHECK(res.headers.count("Content-Type"));
        if (res.headers.count("Content-Type"))
            CHECK("application/octet-stream" == res.headers.find("Content-Type")->second);

        CHECK(res.headers.count("Content-Length"));
        if (res.headers.count("Content-Length"))
            CHECK(to_string(statbuf_cat.st_size) == res.headers.find("Content-Length")->second);
    }

    //Unknown extension check
    {
        request req;
        response res;

        req.url = "/filewith.badext";
        req.http_ver_major = 1;

        CHECK_NOTHROW(app.handle_full(req, res));
        CHECK(200 == res.code);
        CHECK(res.headers.count("Content-Type"));
        if (res.headers.count("Content-Type"))
            CHECK("text/plain" == res.headers.find("Content-Type")->second);

        CHECK(res.headers.count("Content-Length"));
        if (res.headers.count("Content-Length"))
            CHECK(to_string(statbuf_badext.st_size) == res.headers.find("Content-Length")->second);
    }
} // send_file

TEST_CASE("stream_response")
{
    SimpleApp app;


    const std::string keyword_ = "hello";
    const size_t repetitions = 250000;
    const size_t key_response_size = keyword_.length() * repetitions;

    std::string key_response;

    for (size_t i = 0; i < repetitions; i++)
        key_response += keyword_;

    CROW_ROUTE(app, "/test")
    ([&key_response](const crow::request&, crow::response& res) {
        res.body = key_response;
        res.end();
    });

    app.validate();

    // running the test on a separate thread to allow the client to sleep
    std::thread runTest([&app, &key_response, key_response_size, keyword_]() {
        auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
        app.wait_for_server_start();
        asio::io_context io_context;
        std::string sendmsg;

        //Total bytes received
        unsigned int received = 0;
        sendmsg = "GET /test HTTP/1.0\r\n\r\n";
        {
            asio::streambuf b;

            asio::ip::tcp::socket c(io_context);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
            c.send(asio::buffer(sendmsg));

            // consuming the headers, since we don't need those for the test
            static char buf[2048];
            size_t received_headers_bytes = 0;

            // Magic number is 102. It's the size of the headers, which is at
            // least how much we need to read. Since the header size may change
            // and break the test, we read twice as much as the header and
            // search in the received data for the first occurrence of keyword_.
            const size_t headers_bytes_and_some = 102 * 2;
            while (received_headers_bytes < headers_bytes_and_some)
                received_headers_bytes += c.receive(asio::buffer(buf + received_headers_bytes,
                                                                 sizeof(buf) / sizeof(buf[0]) - received_headers_bytes));

            const std::string::size_type header_end_pos = std::string(buf, received_headers_bytes).find(keyword_);
            received += received_headers_bytes - header_end_pos; // add any extra that might have been received to the proper received count

            while (received < key_response_size)
            {
                asio::streambuf::mutable_buffers_type bufs = b.prepare(16384);

                size_t n(0);
                n = c.receive(bufs);
                b.commit(n);
                received += n;

                std::istream istream(&b);
                std::string s;
                istream >> s;

                CHECK(key_response.substr(received - n, n) == s);
            }
        }
        app.stop();
    });
    runTest.join();
} // stream_response

#ifdef CROW_ENABLE_COMPRESSION
TEST_CASE("zlib_compression")
{
    static char buf_deflate[2048];
    static char buf_gzip[2048];

    SimpleApp app_deflate, app_gzip;

    std::string expected_string = "Although moreover mistaken kindness me feelings do be marianne. Son over own nay with tell they cold upon are. "
                                  "Cordial village and settled she ability law herself. Finished why bringing but sir bachelor unpacked any thoughts. "
                                  "Unpleasing unsatiable particular inquietude did nor sir. Get his declared appetite distance his together now families. "
                                  "Friends am himself at on norland it viewing. Suspected elsewhere you belonging continued commanded she.";

    // test deflate
    CROW_ROUTE(app_deflate, "/test_compress")
    ([&]() {
        return expected_string;
    });

    CROW_ROUTE(app_deflate, "/test")
    ([&](const request&, response& res) {
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    // test gzip
    CROW_ROUTE(app_gzip, "/test_compress")
    ([&]() {
        return expected_string;
    });

    CROW_ROUTE(app_gzip, "/test")
    ([&](const request&, response& res) {
        res.compressed = false;

        res.body = expected_string;
        res.end();
    });

    auto t1 = app_deflate.bindaddr(LOCALHOST_ADDRESS).port(45451).use_compression(compression::algorithm::DEFLATE).run_async();
    auto t2 = app_gzip.bindaddr(LOCALHOST_ADDRESS).port(45452).use_compression(compression::algorithm::GZIP).run_async();

    app_deflate.wait_for_server_start();
    app_gzip.wait_for_server_start();

    std::string test_compress_msg = "GET /test_compress\r\nAccept-Encoding: gzip, deflate\r\n\r\n";
    std::string test_compress_no_header_msg = "GET /test_compress\r\n\r\n";
    std::string test_none_msg = "GET /test\r\n\r\n";

    auto inflate_string = [](std::string const& deflated_string) -> const std::string {
        std::string inflated_string;
        Bytef tmp[8192];

        z_stream zstream{};
        zstream.avail_in = deflated_string.size();
        // Nasty const_cast but zlib won't alter its contents
        zstream.next_in = const_cast<Bytef*>(reinterpret_cast<Bytef const*>(deflated_string.c_str()));
        // Initialize with automatic header detection, for gzip support
        if (::inflateInit2(&zstream, MAX_WBITS | 32) == Z_OK)
        {
            do
            {
                zstream.avail_out = sizeof(tmp);
                zstream.next_out = &tmp[0];

                auto ret = ::inflate(&zstream, Z_NO_FLUSH);
                if (ret == Z_OK || ret == Z_STREAM_END)
                {
                    std::copy(&tmp[0], &tmp[sizeof(tmp) - zstream.avail_out], std::back_inserter(inflated_string));
                }
                else
                {
                    // Something went wrong with inflate; make sure we return an empty string
                    inflated_string.clear();
                    break;
                }

            } while (zstream.avail_out == 0);

            // Free zlib's internal memory
            ::inflateEnd(&zstream);
        }

        return inflated_string;
    };

    asio::io_context ic;
    {
        // Compression
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(ic), asio::ip::tcp::socket(ic)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_msg));
            socket[1].send(asio::buffer(test_compress_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate;
            std::string response_gzip;

            for (unsigned i{0}; i < 2048; i++)
            {
                if (buf_deflate[i] == 0)
                {
                    bool end = true;
                    for (unsigned j = i; j < 2048; j++)
                    {
                        if (buf_deflate[j] != 0)
                        {
                            end = false;
                            break;
                        }
                    }
                    if (end)
                    {
                        response_deflate.push_back(0);
                        response_deflate.push_back(0);
                        break;
                    }
                }
                response_deflate.push_back(buf_deflate[i]);
            }

            for (unsigned i{0}; i < 2048; i++)
            {
                if (buf_gzip[i] == 0)
                {
                    bool end = true;
                    for (unsigned j = i; j < 2048; j++)
                    {
                        if (buf_gzip[j] != 0)
                        {
                            end = false;
                            break;
                        }
                    }
                    if (end)
                    {
                        response_deflate.push_back(0);
                        response_deflate.push_back(0);
                        break;
                    }
                }
                response_gzip.push_back(buf_gzip[i]);
            }

            response_deflate = inflate_string(response_deflate.substr(response_deflate.find("\r\n\r\n") + 4));
            response_gzip = inflate_string(response_gzip.substr(response_gzip.find("\r\n\r\n") + 4));

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
        // No Header (thus no compression)
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(ic), asio::ip::tcp::socket(ic)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_compress_no_header_msg));
            socket[1].send(asio::buffer(test_compress_no_header_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate(buf_deflate);
            std::string response_gzip(buf_gzip);
            response_deflate = response_deflate.substr(response_deflate.find("\r\n\r\n") + 4);
            response_gzip = response_gzip.substr(response_gzip.find("\r\n\r\n") + 4);

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
        // No compression
        {
            asio::ip::tcp::socket socket[2] = {asio::ip::tcp::socket(ic), asio::ip::tcp::socket(ic)};
            socket[0].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
            socket[1].connect(asio::ip::tcp::endpoint(asio::ip::make_address(LOCALHOST_ADDRESS), 45452));

            socket[0].send(asio::buffer(test_none_msg));
            socket[1].send(asio::buffer(test_none_msg));

            socket[0].receive(asio::buffer(buf_deflate, 2048));
            socket[1].receive(asio::buffer(buf_gzip, 2048));

            std::string response_deflate(buf_deflate);
            std::string response_gzip(buf_gzip);
            response_deflate = response_deflate.substr(response_deflate.find("\r\n\r\n") + 4);
            response_gzip = response_gzip.substr(response_gzip.find("\r\n\r\n") + 4);

            socket[0].close();
            socket[1].close();
            CHECK(expected_string == response_deflate);
            CHECK(expected_string == response_gzip);
        }
    }

    app_deflate.stop();
    app_gzip.stop();
} // zlib_compression
#endif

TEST_CASE("catchall")
{
    SimpleApp app;
    SimpleApp app2;

    CROW_ROUTE(app, "/place")
    ([]() {
        return "place";
    });

    CROW_CATCHALL_ROUTE(app)
    ([](response& res) {
        res.body = "!place";
    });

    CROW_ROUTE(app2, "/place")
    ([]() {
        return "place";
    });

    app.validate();
    app2.validate();

    {
        request req;
        response res;

        req.url = "/place";

        app.handle_full(req, res);

        CHECK(200 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/another_place";

        app.handle_full(req, res);

        CHECK(404 == res.code);
        CHECK("!place" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/place";

        app2.handle_full(req, res);

        CHECK(200 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/another_place";

        app2.handle_full(req, res);

        CHECK(404 == res.code);
    }
} // catchall


TEST_CASE("catchall_check_full_handling")
{
    struct NotLocalMiddleware {
        bool before_handle_called{false};
        bool after_handle_called{false};

        struct context {
        };

        void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {
            before_handle_called = true;
        }

        void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {
            after_handle_called = true;
        }
    };

    App<NotLocalMiddleware> app;
    bool headers_empty=true;
    CROW_ROUTE(app, "/")
    ([]() {
        return "works!";
    });

    CROW_CATCHALL_ROUTE(app) ([&headers_empty](const crow::request& req,response& res) {
        headers_empty = req.headers.empty();
        auto req_h = req.headers;
        auto res_h = res.headers;
        res.add_header("bla","bla_val");
        res.body = "bla_body";

    });

    app.validate();

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).server_name("lol").run_async();
    app.wait_for_server_start();

    {
        // call not handled url to get response from catch_all handler
        auto resp = HttpClient::request(LOCALHOST_ADDRESS, 45451,
                                        "GET /catch_all HTTP/1.0\r\nHost: localhost\r\n\r\n");

        CHECK(resp.find("bla") != std::string::npos);

        auto global_middleware = app.get_middleware<NotLocalMiddleware>();
        CHECK(global_middleware.after_handle_called==true);
        CHECK(global_middleware.before_handle_called==true);
        CHECK(headers_empty==false);

    }

    app.stop();
} // local_middleware


TEST_CASE("blueprint")
{
    SimpleApp app;
    crow::Blueprint bp("bp_prefix", "cstat", "ctemplate");
    crow::Blueprint bp_not_sub("bp_prefix_second");
    crow::Blueprint sub_bp("bp2", "csstat", "cstemplate");
    crow::Blueprint sub_sub_bp("bp3");

    CROW_BP_ROUTE(sub_bp, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(bp_not_sub, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(sub_sub_bp, "/hi")
    ([]() {
        return "Hi world!";
    });

    CROW_BP_CATCHALL_ROUTE(sub_bp)
    ([]() {
        return response(200, "WRONG!!");
    });

    app.register_blueprint(bp);
    app.register_blueprint(bp_not_sub);
    bp.register_blueprint(sub_bp);
    sub_bp.register_blueprint(sub_sub_bp);

    app.add_blueprint();
    app.add_static_dir();
    app.validate();

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/hello";

        app.handle_full(req, res);

        CHECK("Hello world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix_second/hello";

        app.handle_full(req, res);

        CHECK("Hello world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/bp3/hi";

        app.handle_full(req, res);

        CHECK("Hi world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/nonexistent";

        app.handle_full(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix_second/nonexistent";

        app.handle_full(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/nonexistent";

        app.handle_full(req, res);

        CHECK(200 == res.code);
        CHECK("WRONG!!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/bp3/nonexistent";

        app.handle_full(req, res);

        CHECK(200 == res.code);
        CHECK("WRONG!!" == res.body);
    }
} // blueprint

TEST_CASE("exception_handler")
{
    SimpleApp app;

    CROW_ROUTE(app, "/get_error")
    ([&]() -> std::string {
        throw std::runtime_error("some error occurred");
    });

    CROW_ROUTE(app, "/get_no_error")
    ([&]() {
        return "Hello world";
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/get_error";
        app.handle_full(req, res);

        CHECK(500 == res.code);
        CHECK(res.body.empty());
    }

    {
        request req;
        response res;

        req.url = "/get_no_error";
        app.handle_full(req, res);

        CHECK(200 == res.code);
        CHECK(res.body.find("Hello world") != std::string::npos);
    }

    app.exception_handler([](crow::response& res) {
        try
        {
            throw;
        }
        catch (const std::exception& e)
        {
            res = response(501, e.what());
        }
    });

    {
        request req;
        response res;

        req.url = "/get_error";
        app.handle_full(req, res);

        CHECK(501 == res.code);
        CHECK(res.body.find("some error occurred") != std::string::npos);
    }

    {
        request req;
        response res;

        req.url = "/get_no_error";
        app.handle_full(req, res);

        CHECK(200 == res.code);
        CHECK(res.body.find("some error occurred") == std::string::npos);
        CHECK(res.body.find("Hello world") != std::string::npos);
    }
} // exception_handler

TEST_CASE("get_port")
{
    SimpleApp app;

    constexpr std::uint16_t port = 12345;

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
        asio::io_context ic;
        std::string sendmsg = "GET /\r\n\r\n";
        future_status status;

        {
            asio::ip::tcp::socket c(ic);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::make_address(LOCALHOST_ADDRESS), 45451));

            auto receive_future = async(launch::async, [&]() {
                asio_error_code ec;
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
            asio::ip::tcp::socket c(ic);
            c.connect(asio::ip::tcp::endpoint(
              asio::ip::make_address(LOCALHOST_ADDRESS), 45451));

            size_t received;
            auto receive_future = async(launch::async, [&]() {
                asio_error_code ec;
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

TEST_CASE("task_timer")
{
    using work_guard_type = asio::executor_work_guard<asio::io_context::executor_type>;

    asio::io_context io_context;
    work_guard_type work_guard(io_context.get_executor());
    thread io_thread([&io_context]() {
        io_context.run();
    });

    bool a = false;
    bool b = false;

    crow::detail::task_timer timer(io_context, std::chrono::milliseconds(100));
    CHECK(timer.get_default_timeout() == 5);
    timer.set_default_timeout(9);
    CHECK(timer.get_default_timeout() == 9);

    timer.schedule([&a]() {
        a = true;
    },
                   5);
    timer.schedule([&b]() {
        b = true;
    });

    asio::steady_timer t(io_context);
    asio_error_code ec;

    t.expires_after(3 * timer.get_tick_length());
    t.wait(ec);
    // we are at 3 ticks, nothing be changed yet
    CHECK(!ec);
    CHECK(a == false);
    CHECK(b == false);
    t.expires_after(3 * timer.get_tick_length());
    t.wait(ec);
    // we are at 3+3 = 6 ticks, so first task_timer handler should have runned
    CHECK(!ec);
    CHECK(a == true);
    CHECK(b == false);

    t.expires_after(5 * timer.get_tick_length());
    t.wait(ec);
    //we are at 3+3 +5 = 11 ticks, both task_timer handlers shoudl have run now
    CHECK(!ec);
    CHECK(a == true);
    CHECK(b == true);

    io_context.stop();
    io_thread.join();
} // task_timer

TEST_CASE("http2_upgrade_is_ignored")
{
    // Crow does not support HTTP/2 so upgrade headers must be ignored
    // relevant RFC: https://datatracker.ietf.org/doc/html/rfc7540#section-3.2

    static char buf[5012];

    SimpleApp app;
    CROW_ROUTE(app, "/echo").methods("POST"_method)([](crow::request const& req) {
        return req.body;
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();

    app.wait_for_server_start();
    asio::io_context ic;

    auto make_request = [&](const std::string& rq) {
        asio::ip::tcp::socket c(ic);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(rq));
        c.receive(asio::buffer(buf, 2048));
        c.close();
        return std::string(buf);
    };

    std::string request =
      "POST /echo HTTP/1.1\r\n"
      "user-agent: unittest.cpp\r\n"
      "host: " LOCALHOST_ADDRESS ":45451\r\n"
      "content-length: 48\r\n"
      "connection: upgrade\r\n"
      "upgrade: h2c\r\n"
      "\r\n"
      "http2 upgrade is not supported so body is parsed\r\n"
      "\r\n";

    auto res = make_request(request);
    CHECK(res.find("http2 upgrade is not supported so body is parsed") != std::string::npos);
    app.stop();
}

TEST_CASE("unix_socket")
{
    static char buf[2048];
    SimpleApp app;
    CROW_ROUTE(app, "/").methods("GET"_method)([] {
        return "A";
    });

    constexpr const char* socket_path = "unittest.sock";
    unlink(socket_path);
    auto _ = app.local_socket_path(socket_path).run_async();
    app.wait_for_server_start();

    std::string sendmsg = "GET / HTTP/1.0\r\n\r\n";
    {
        asio::io_context ic;
        asio::local::stream_protocol::socket c(ic);
        c.connect(asio::local::stream_protocol::endpoint(socket_path));

        c.send(asio::buffer(sendmsg));

        size_t recved = c.receive(asio::buffer(buf, 2048));
        CHECK('A' == buf[recved - 1]);
    }
    app.stop();
} // unix_socket

TEST_CASE("option_header_passed_in_full")
{
    const std::string ServerName = "AN_EXTREMELY_UNIQUE_SERVER_NAME";

    crow::App<crow::CORSHandler>
      app;

    app.get_middleware<crow::CORSHandler>() //
      .global()
      .allow_credentials()
      .expose("X-Total-Pages", "X-Total-Entries", "Content-Disposition");

    app.server_name(ServerName);


    CROW_ROUTE(app, "/echo").methods(crow::HTTPMethod::Options)([]() {
        crow::response response{};
        response.add_header("Key", "Value");
        return response;
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();

    app.wait_for_server_start();

    asio::io_context is;

    auto make_request = [&](const std::string& rq) {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::make_address(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(rq));
        std::string fullString{};
        asio_error_code error;
        char buffer[1024];
        while (true)
        {
            size_t len = c.read_some(asio::buffer(buffer), error);

            if (error == asio::error::eof)
            {
                break;
            }
            else if (error)
            {
                throw system_error(error);
            }

            fullString.append(buffer, len);
        }
        c.close();
        return fullString;
    };

    std::string request =
      "OPTIONS /echo HTTP/1.1\r\n";

    auto res = make_request(request);
    CHECK(res.find(ServerName) != std::string::npos);
    app.stop();
}
