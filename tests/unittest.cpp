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
        CHECK(-2.71828 == C);
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
        CHECK(3.141592 == C);
        CHECK("hello_there" == D);
        CHECK("a/b/c/d" == E);
    }
} // RoutingTest

TEST_CASE("simple_response_routing_params")
{
    CHECK(100 == response(100).code);
    CHECK(200 == response("Hello there").code);
    CHECK(500 == response(500, "Internal Error?").code);

    CHECK(100 == response(100, "xml", "").code);
    CHECK("text/xml" == response(100, "xml", "").get_header_value("Content-Type"));
    CHECK(200 == response(200, "html", "").code);
    CHECK("text/html" == response(200, "html", "").get_header_value("Content-Type"));
    CHECK(500 == response(500, "html", "Internal Error?").code);
    CHECK("text/css" == response(500, "css", "Internal Error?").get_header_value("Content-Type"));

    routing_params rp;
    rp.int_params.push_back(1);
    rp.int_params.push_back(5);
    rp.uint_params.push_back(2);
    rp.double_params.push_back(3);
    rp.string_params.push_back("hello");
    CHECK(1 == rp.get<int64_t>(0));
    CHECK(5 == rp.get<int64_t>(1));
    CHECK(2 == rp.get<uint64_t>(0));
    CHECK(3 == rp.get<double>(0));
    CHECK("hello" == rp.get<string>(0));
} // simple_response_routing_params

TEST_CASE("custom_content_types")
{
    // standard behaviour: content type is a key of mime_types
    CHECK("text/html" == response("html", "").get_header_value("Content-Type"));
    CHECK("image/jpeg" == response("jpg", "").get_header_value("Content-Type"));
    CHECK("video/mpeg" == response("mpg", "").get_header_value("Content-Type"));

    // content type is already a valid mime type
    CHECK("text/csv" == response("text/csv", "").get_header_value("Content-Type"));
    CHECK("application/xhtml+xml" == response("application/xhtml+xml", "").get_header_value("Content-Type"));
    CHECK("font/custom;parameters=ok" == response("font/custom;parameters=ok", "").get_header_value("Content-Type"));

    // content type looks like a mime type, but is invalid
    // note: RFC6838 only allows a limited set of parent types:
    // https://datatracker.ietf.org/doc/html/rfc6838#section-4.2.7
    //
    // These types are: application, audio, font, example, image, message,
    //                  model, multipart, text, video

    CHECK("text/plain" == response("custom/type", "").get_header_value("Content-Type"));

    // content type does not look like a mime type.
    CHECK("text/plain" == response("notarealextension", "").get_header_value("Content-Type"));
    CHECK("text/plain" == response("image/", "").get_header_value("Content-Type"));
    CHECK("text/plain" == response("/json", "").get_header_value("Content-Type"));

} // custom_content_types

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

TEST_CASE("json_read")
{
    {
        const char* json_error_tests[] = {
          "{} 3",
          "{{}",
          "{3}",
          "3.4.5",
          "+3",
          "3-2",
          "00",
          "03",
          "1e3e3",
          "1e+.3",
          "nll",
          "f",
          "t",
          "{\"x\":3,}",
          "{\"x\"}",
          "{\"x\":3   q}",
          "{\"x\":[3 4]}",
          "{\"x\":[\"",
          "{\"x\":[[], 4],\"y\",}",
          "{\"x\":[3",
          "{\"x\":[ null, false, true}",
        };
        for (auto s : json_error_tests)
        {
            auto x = json::load(s);
            if (x)
            {
                FAIL_CHECK(std::string("should fail to parse ") + s);
                return;
            }
        }
    }

    auto x = json::load(R"({"message":"hello, world"})");
    if (!x) FAIL_CHECK("fail to parse");
    CHECK("hello, world" == x["message"]);
    CHECK(1 == x.size());
    CHECK(false == x.has("mess"));
    REQUIRE_THROWS(x["mess"]);
    // TODO(ipkn) returning false is better than exception
    // ASSERT_THROW(3 == x["message"]);
    CHECK(12 == x["message"].size());

    std::string s =
      R"({"int":3,     "ints"  :[1,2,3,4,5],	"bigint":1234567890	})";
    auto y = json::load(s);
    CHECK(3 == y["int"]);
    CHECK(3.0 == y["int"]);
    CHECK(3.01 != y["int"]);
    CHECK(5 == y["ints"].size());
    CHECK(1 == y["ints"][0]);
    CHECK(2 == y["ints"][1]);
    CHECK(3 == y["ints"][2]);
    CHECK(4 == y["ints"][3]);
    CHECK(5 == y["ints"][4]);
    CHECK(1u == y["ints"][0]);
    CHECK(1.f == y["ints"][0]);

    int q = (int)y["ints"][1];
    CHECK(2 == q);
    q = y["ints"][2].i();
    CHECK(3 == q);
    CHECK(1234567890 == y["bigint"]);

    std::string s2 = R"({"bools":[true, false], "doubles":[1.2, -3.4]})";
    auto z = json::load(s2);
    CHECK(2 == z["bools"].size());
    CHECK(2 == z["doubles"].size());
    CHECK(true == z["bools"][0].b());
    CHECK(false == z["bools"][1].b());
    CHECK(1.2 == z["doubles"][0].d());
    CHECK(-3.4 == z["doubles"][1].d());

    std::string s3 = R"({"uint64": 18446744073709551615})";
    auto z1 = json::load(s3);
    CHECK(18446744073709551615ull == z1["uint64"].u());

    std::ostringstream os;
    os << z1["uint64"];
    CHECK("18446744073709551615" == os.str());
} // json_read

TEST_CASE("json_read_real")
{
    vector<std::string> v{"0.036303908355795146",
                          "0.18320417789757412",
                          "0.05319940476190476",
                          "0.15224702380952382",
                          "0",
                          "0.3296201145552561",
                          "0.47921580188679247",
                          "0.05873511904761905",
                          "0.1577827380952381",
                          "0.4996841307277628",
                          "0.6425412735849056",
                          "0.052113095238095236",
                          "0.12830357142857143",
                          "0.7871041105121294",
                          "0.954220013477089",
                          "0.05869047619047619",
                          "0.1625",
                          "0.8144794474393531",
                          "0.9721613881401617",
                          "0.1399404761904762",
                          "0.24470238095238095",
                          "0.04527459568733154",
                          "0.2096950808625337",
                          "0.35267857142857145",
                          "0.42791666666666667",
                          "0.855731974393531",
                          "0.9352467991913747",
                          "0.3816220238095238",
                          "0.4282886904761905",
                          "0.39414167789757415",
                          "0.5316079851752021",
                          "0.3809375",
                          "0.4571279761904762",
                          "0.03522995283018868",
                          "0.1915641846361186",
                          "0.6164136904761904",
                          "0.7192708333333333",
                          "0.05675117924528302",
                          "0.21308541105121293",
                          "0.7045386904761904",
                          "0.8016815476190476"};
    for (auto x : v)
    {
        CROW_LOG_DEBUG << x;
        CHECK(json::load(x).d() == utility::lexical_cast<double>(x));
    }

    auto ret = json::load(
      R"---({"balloons":[{"mode":"ellipse","left":0.036303908355795146,"right":0.18320417789757412,"top":0.05319940476190476,"bottom":0.15224702380952382,"index":"0"},{"mode":"ellipse","left":0.3296201145552561,"right":0.47921580188679247,"top":0.05873511904761905,"bottom":0.1577827380952381,"index":"1"},{"mode":"ellipse","left":0.4996841307277628,"right":0.6425412735849056,"top":0.052113095238095236,"bottom":0.12830357142857143,"index":"2"},{"mode":"ellipse","left":0.7871041105121294,"right":0.954220013477089,"top":0.05869047619047619,"bottom":0.1625,"index":"3"},{"mode":"ellipse","left":0.8144794474393531,"right":0.9721613881401617,"top":0.1399404761904762,"bottom":0.24470238095238095,"index":"4"},{"mode":"ellipse","left":0.04527459568733154,"right":0.2096950808625337,"top":0.35267857142857145,"bottom":0.42791666666666667,"index":"5"},{"mode":"ellipse","left":0.855731974393531,"right":0.9352467991913747,"top":0.3816220238095238,"bottom":0.4282886904761905,"index":"6"},{"mode":"ellipse","left":0.39414167789757415,"right":0.5316079851752021,"top":0.3809375,"bottom":0.4571279761904762,"index":"7"},{"mode":"ellipse","left":0.03522995283018868,"right":0.1915641846361186,"top":0.6164136904761904,"bottom":0.7192708333333333,"index":"8"},{"mode":"ellipse","left":0.05675117924528302,"right":0.21308541105121293,"top":0.7045386904761904,"bottom":0.8016815476190476,"index":"9"}]})---");
    CHECK(ret);
} // json_read_real

TEST_CASE("json_read_unescaping")
{
    {
        auto x = json::load(R"({"data":"\ud55c\n\t\r"})");
        if (!x)
        {
            FAIL_CHECK("fail to parse");
            return;
        }
        CHECK(6 == x["data"].size());
        CHECK("한\n\t\r" == x["data"]);
    }
    {
        // multiple r_string instance
        auto x = json::load(R"({"data":"\ud55c\n\t\r"})");
        auto a = x["data"].s();
        auto b = x["data"].s();
        CHECK(6 == a.size());
        CHECK(6 == b.size());
        CHECK(6 == x["data"].size());
    }
} // json_read_unescaping

TEST_CASE("json_read_string")
{
    auto x = json::load(R"({"message": 53})");
    int y(x["message"]);
    std::string z(x["message"]);
    CHECK(53 == y);
    CHECK("53" == z);
} // json_read_string

TEST_CASE("json_read_container")
{
    auto x = json::load(R"({"first": 53, "second": "55", "third": [5,6,7,8,3,2,1,4]})");
    CHECK(std::vector<std::string>({"first", "second", "third"}) == x.keys());
    CHECK(53 == int(x.lo()[0]));
    CHECK("55" == std::string(x.lo()[1]));
    CHECK(8 == int(x.lo()[2].lo()[3]));
} // json_read_container

TEST_CASE("json_write")
{
    json::wvalue x;
    x["message"] = "hello world";
    CHECK(R"({"message":"hello world"})" == x.dump());
    x["message"] = std::string("string value");
    CHECK(R"({"message":"string value"})" == x.dump());
    x["message"]["x"] = 3;
    CHECK(R"({"message":{"x":3}})" == x.dump());
    x["message"]["y"] = 5;
    CHECK((R"({"message":{"x":3,"y":5}})" == x.dump() ||
           R"({"message":{"y":5,"x":3}})" == x.dump()));
    x["message"] = 5.5;
    CHECK(R"({"message":5.5})" == x.dump());
    x["message"] = 1234567890;
    CHECK(R"({"message":1234567890})" == x.dump());

    json::wvalue y;
    y["scores"][0] = 1;
    y["scores"][1] = "king";
    y["scores"][2] = 3.5;
    CHECK(R"({"scores":[1,"king",3.5]})" == y.dump());

    y["scores"][2][0] = "real";
    y["scores"][2][1] = false;
    y["scores"][2][2] = true;
    CHECK(R"({"scores":[1,"king",["real",false,true]]})" == y.dump());

    y["scores"]["a"]["b"]["c"] = nullptr;
    CHECK(R"({"scores":{"a":{"b":{"c":null}}}})" == y.dump());

    y["scores"] = std::vector<int>{1, 2, 3};
    CHECK(R"({"scores":[1,2,3]})" == y.dump());
} // json_write

TEST_CASE("json_write_with_indent")
{
    static constexpr int IndentationLevelOne = 1;
    static constexpr int IndentationLevelTwo = 2;
    static constexpr int IndentationLevelFour = 4;

    json::wvalue y;

    y["scores"][0] = 1;
    y["scores"][1] = "king";
    y["scores"][2][0] = "real";
    y["scores"][2][1] = false;
    y["scores"][2][2] = true;

    CHECK(R"({
 "scores": [
  1,
  "king",
  [
   "real",
   false,
   true
  ]
 ]
})" == y.dump(IndentationLevelOne));

    CHECK(R"({
  "scores": [
    1,
    "king",
    [
      "real",
      false,
      true
    ]
  ]
})" == y.dump(IndentationLevelTwo));

    CHECK(R"({
    "scores": [
        1,
        "king",
        [
            "real",
            false,
            true
        ]
    ]
})" == y.dump(IndentationLevelFour));

    static constexpr char TabSeparator = '\t';

    // Note: The following string needs to use tabs!
    CHECK(R"({
	"scores": [
		1,
		"king",
		[
			"real",
			false,
			true
		]
	]
})" == y.dump(IndentationLevelOne, TabSeparator));
} // json_write_with_indent


TEST_CASE("json_copy_r_to_w_to_w_to_r")
{
    json::rvalue r = json::load(
      R"({"smallint":2,"bigint":2147483647,"fp":23.43,"fpsc":2.343e1,"str":"a string","trueval":true,"falseval":false,"nullval":null,"listval":[1,2,"foo","bar"],"obj":{"member":23,"other":"baz"}})");
    json::wvalue v{r};
    json::wvalue w(v);
    json::rvalue x =
      json::load(w.dump()); // why no copy-ctor wvalue -> rvalue?
    CHECK(2 == x["smallint"]);
    CHECK(2147483647 == x["bigint"]);
    CHECK(23.43 == x["fp"]);
    CHECK(23.43 == x["fpsc"]);
    CHECK("a string" == x["str"]);
    CHECK(x["trueval"].b());
    REQUIRE_FALSE(x["falseval"].b());
    CHECK(json::type::Null == x["nullval"].t());
    CHECK(4u == x["listval"].size());
    CHECK(1 == x["listval"][0]);
    CHECK(2 == x["listval"][1]);
    CHECK("foo" == x["listval"][2]);
    CHECK("bar" == x["listval"][3]);
    CHECK(23 == x["obj"]["member"]);
    CHECK("member" == x["obj"]["member"].key());
    CHECK("baz" == x["obj"]["other"]);
    CHECK("other" == x["obj"]["other"].key());
} // json_copy_r_to_w_to_w_to_r
//TODO(EDev): maybe combine these

TEST_CASE("json::wvalue::wvalue(bool)")
{
    CHECK(json::wvalue(true).t() == json::type::True);
    CHECK(json::wvalue(false).t() == json::type::False);
} // json::wvalue::wvalue(bool)

TEST_CASE("json::wvalue::wvalue(std::uint8_t)")
{
    std::uint8_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint8_t)

TEST_CASE("json::wvalue::wvalue(std::uint16_t)")
{
    std::uint16_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint16_t)

TEST_CASE("json::wvalue::wvalue(std::uint32_t)")
{
    std::uint32_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint32_t)

TEST_CASE("json::wvalue::wvalue(std::uint64_t)")
{
    std::uint64_t i = 42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "42");
} // json::wvalue::wvalue(std::uint64_t)

TEST_CASE("json::wvalue::wvalue(std::int8_t)")
{
    std::int8_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int8_t)

TEST_CASE("json::wvalue::wvalue(std::int16_t)")
{
    std::int16_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int16_t)

TEST_CASE("json::wvalue::wvalue(std::int32_t)")
{
    std::int32_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int32_t)

TEST_CASE("json::wvalue::wvalue(std::int64_t)")
{
    std::int64_t i = -42;
    json::wvalue value = i;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "-42");
} // json::wvalue::wvalue(std::int64_t)

TEST_CASE("json::wvalue::wvalue(float)")
{
    float f = 4.2;
    json::wvalue value = f;

    CHECK(value.t() == json::type::Number);
    CHECK(value.dump() == "4.2");
} // json::wvalue::wvalue(float)

TEST_CASE("json::wvalue::wvalue(double)")
{
    double d = 0.036303908355795146;
    json::wvalue value = d;

    CHECK(value.t() == json::type::Number);
    auto dumped_value = value.dump();
    CROW_LOG_DEBUG << dumped_value;
    CHECK(std::abs(utility::lexical_cast<double>(dumped_value) - d) < numeric_limits<double>::epsilon());
} // json::wvalue::wvalue(double)

TEST_CASE("json::wvalue::wvalue(char const*)")
{
    char const* str = "Hello world!";
    json::wvalue value = str;

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(char const*)

TEST_CASE("json::wvalue::wvalue(std::string const&)")
{
    std::string str = "Hello world!";
    json::wvalue value = str;

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(std::string const&)

TEST_CASE("json::wvalue::wvalue(std::string&&)")
{
    std::string str = "Hello world!";
    json::wvalue value = std::move(str);

    CHECK(value.t() == json::type::String);
    CHECK(value.dump() == "\"Hello world!\"");
} // json::wvalue::wvalue(std::string&&)

TEST_CASE("json::wvalue::wvalue(std::initializer_list<std::pair<std::string const, json::wvalue>>)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    /* initializer-list constructor. */
    json::wvalue value({{"integer", integer},
                        {"number", number},
                        {"truth", truth},
                        {"lie", lie},
                        {"null", null}});

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::initializer_list<std::pair<std::string const, json::wvalue>>)

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue> const&)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value(map); /* copy-constructor. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue> const&)

TEST_CASE("json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue>&&)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map = {{{"integer", integer},
                                 {"number", number},
                                 {"truth", truth},
                                 {"lie", lie},
                                 {"null", null}}};

    json::wvalue value(std::move(map)); /* move constructor. */
    // json::wvalue value = std::move(map); /* move constructor. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::wvalue(std::[unordered_]map<std::string, json::wvalue>&&)

TEST_CASE("json::wvalue::operator=(std::initializer_list<std::pair<std::string const, json::wvalue>>)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue value;
    /* initializer-list assignment. */
    value = {
      {"integer", integer},
      {"number", number},
      {"truth", truth},
      {"lie", lie},
      {"null", null}};

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::initializer_list<std::pair<std::string const, json::wvalue>>)

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue> const&)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value;
    value = map; /* copy assignment. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue> const&)

TEST_CASE("json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue>&&)")
{
    json::wvalue integer, number, truth, lie, null;
    integer = 2147483647;
    number = 23.43;
    truth = true;
    lie = false;

    json::wvalue::object map({{"integer", integer},
                              {"number", number},
                              {"truth", truth},
                              {"lie", lie},
                              {"null", null}});

    json::wvalue value;
    value = std::move(map); /* move assignment. */

    CHECK(value["integer"].dump() == integer.dump());
    CHECK(value["number"].dump() == number.dump());
    CHECK(value["truth"].dump() == truth.dump());
    CHECK(value["lie"].dump() == lie.dump());
    CHECK(value["null"].dump() == null.dump());
} // json::wvalue::operator=(std::[unordered_]map<std::string, json::wvalue>&&)

TEST_CASE("json_vector")
{
    json::wvalue a;
    json::wvalue b;
    json::wvalue c;
    json::wvalue d;
    json::wvalue e;
    json::wvalue f;
    json::wvalue g;
    json::wvalue h;
    a = 5;
    b = 6;
    c = 7;
    d = 8;
    e = 4;
    f = 3;
    g = 2;
    h = 1;
    std::vector<json::wvalue> nums;
    nums.emplace_back(a);
    nums.emplace_back(b);
    nums.emplace_back(c);
    nums.emplace_back(d);
    nums.emplace_back(e);
    nums.emplace_back(f);
    nums.emplace_back(g);
    nums.emplace_back(h);
    json::wvalue x(nums);

    CHECK(8 == x.size());
    CHECK("[5,6,7,8,4,3,2,1]" == x.dump());
} // json_vector

TEST_CASE("json_list")
{
    json::wvalue x(json::wvalue::list({5, 6, 7, 8, 4, 3, 2, 1}));

    CHECK(8 == x.size());
    CHECK("[5,6,7,8,4,3,2,1]" == x.dump());
} // json_list

static crow::json::wvalue getValue(int i){
     return crow::json::wvalue(i);
}

TEST_CASE("json Incorrect move of wvalue class #953")
{
    {
        crow::json::wvalue int_value(-500);
        crow::json::wvalue copy_value(std::move(int_value));

        REQUIRE(copy_value.dump()=="-500");
    }
    {
         crow::json::wvalue json;
         json["int_value"] = getValue(-500);
         REQUIRE(json["int_value"].dump()=="-500");
    }
}

TEST_CASE("template_basic")
{
    auto t = crow::mustache::compile(R"---(attack of {{name}})---");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes" == result);
} // template_basic

TEST_CASE("template_true_tag")
{
    auto t = crow::mustache::compile(R"---({{true_value}})---");
    crow::mustache::context ctx;
    ctx["true_value"] = true;
    auto result = t.render_string(ctx);
    CHECK("true" == result);
} // template_true_tag

TEST_CASE("template_false_tag")
{
    auto t = crow::mustache::compile(R"---({{false_value}})---");
    crow::mustache::context ctx;
    ctx["false_value"] = false;
    auto result = t.render_string(ctx);
    CHECK("false" == result);
} // template_false_tag

TEST_CASE("template_function")
{
    auto t = crow::mustache::compile("attack of {{func}}");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    ctx["func"] = std::function<std::string(std::string)>([&](std::string) {
        return std::string("{{name}}, IN SPACE!");
    });
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes, IN SPACE!" == result);
}

TEST_CASE("template_load")
{
    crow::mustache::set_base(".");
    ofstream("test.mustache") << R"---(attack of {{name}})---";
    auto t = crow::mustache::load("test.mustache");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes" == result);
    unlink("test.mustache");
} // template_load

TEST_CASE("TemplateRouting")
{
    SimpleApp app;

    CROW_ROUTE(app, "/temp")
    ([] {
        auto t = crow::mustache::compile(R"---(attack of {{name}})---");
        crow::mustache::context ctx;
        ctx["name"] = "killer tomatoes";
        return crow::response(t.render(ctx));
    });

    app.validate();

    {
        request req;
        response res;

        req.url = "/temp";

        app.handle_full(req, res);

        CHECK("attack of killer tomatoes" == res.body);
        CHECK("text/html" == crow::get_header_value(res.headers, "Content-Type"));
    }
} // PathRouting

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
    asio::ip::address adr = asio::ip::make_address(LOCALHOST_ADDRESS);
    tcp::endpoint ep(adr,45451);
    decltype(app)::server_t server(&app, ep);
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
    CHECK(utility::lexical_cast<double>(last_url_params.get("double")) ==
          123.45);
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

TEST_CASE("websocket")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nHost: localhost\r\n\r\n";

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

TEST_CASE("websocket_close")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nHost: localhost\r\n\r\n";

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

TEST_CASE("websocket_missing_host")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";

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

TEST_CASE("websocket_max_payload")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nHost: localhost\r\n\r\n";

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

TEST_CASE("websocket_subprotocols")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Protocol: myprotocol\r\nSec-WebSocket-Version: 13\r\nHost: localhost\r\n\r\n";

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

TEST_CASE("mirror_websocket_subprotocols")
{
    static std::string http_message = "GET /ws HTTP/1.1\r\nConnection: keep-alive, Upgrade\r\nupgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Protocol: protocol1, protocol2\r\nSec-WebSocket-Version: 13\r\nHost: localhost\r\n\r\n";

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

TEST_CASE("base64")
{
    unsigned char sample_bin[] = {0x14, 0xfb, 0x9c, 0x03, 0xd9, 0x7e};
    std::string sample_bin_str(reinterpret_cast<char const*>(sample_bin),
                               sizeof(sample_bin) / sizeof(sample_bin[0]));
    std::string sample_bin_enc = "FPucA9l+";
    std::string sample_bin_enc_url = "FPucA9l-";
    unsigned char sample_bin1[] = {0x14, 0xfb, 0x9c, 0x03, 0xd9};
    std::string sample_bin1_str(reinterpret_cast<char const*>(sample_bin1),
                                sizeof(sample_bin1) / sizeof(sample_bin1[0]));
    std::string sample_bin1_enc = "FPucA9k=";
    std::string sample_bin1_enc_np = "FPucA9k";
    unsigned char sample_bin2[] = {0x14, 0xfb, 0x9c, 0x03};
    std::string sample_bin2_str(reinterpret_cast<char const*>(sample_bin2),
                                sizeof(sample_bin2) / sizeof(sample_bin2[0]));
    std::string sample_bin2_enc = "FPucAw==";
    std::string sample_bin2_enc_np = "FPucAw";
    std::string sample_text = "Crow Allows users to handle requests that may not come from the network. This is done by calling the handle(req, res) method and providing a request and response objects. Which causes crow to identify and run the appropriate handler, returning the resulting response.";
    std::string sample_base64 = "Q3JvdyBBbGxvd3MgdXNlcnMgdG8gaGFuZGxlIHJlcXVlc3RzIHRoYXQgbWF5IG5vdCBjb21lIGZyb20gdGhlIG5ldHdvcmsuIFRoaXMgaXMgZG9uZSBieSBjYWxsaW5nIHRoZSBoYW5kbGUocmVxLCByZXMpIG1ldGhvZCBhbmQgcHJvdmlkaW5nIGEgcmVxdWVzdCBhbmQgcmVzcG9uc2Ugb2JqZWN0cy4gV2hpY2ggY2F1c2VzIGNyb3cgdG8gaWRlbnRpZnkgYW5kIHJ1biB0aGUgYXBwcm9wcmlhdGUgaGFuZGxlciwgcmV0dXJuaW5nIHRoZSByZXN1bHRpbmcgcmVzcG9uc2Uu";


    CHECK(crow::utility::base64encode(sample_text, sample_text.length()) == sample_base64);
    CHECK(crow::utility::base64encode(sample_bin, 6) == sample_bin_enc);
    CHECK(crow::utility::base64encode_urlsafe(sample_bin, 6) == sample_bin_enc_url);
    CHECK(crow::utility::base64encode(sample_bin1, 5) == sample_bin1_enc);
    CHECK(crow::utility::base64encode(sample_bin2, 4) == sample_bin2_enc);

    CHECK(crow::utility::base64decode(sample_base64) == sample_text);
    CHECK(crow::utility::base64decode(sample_base64, sample_base64.length()) == sample_text);
    CHECK(crow::utility::base64decode(sample_bin_enc, 8) == sample_bin_str);
    CHECK(crow::utility::base64decode(sample_bin_enc_url, 8) == sample_bin_str);
    CHECK(crow::utility::base64decode(sample_bin1_enc, 8) == sample_bin1_str);
    CHECK(crow::utility::base64decode(sample_bin1_enc_np, 7) == sample_bin1_str);
    CHECK(crow::utility::base64decode(sample_bin2_enc, 8) == sample_bin2_str);
    CHECK(crow::utility::base64decode(sample_bin2_enc_np, 6) == sample_bin2_str);
} // base64

TEST_CASE("normalize_path")
{
    CHECK(crow::utility::normalize_path("/abc/def") == "/abc/def/");
    CHECK(crow::utility::normalize_path("path\\to\\directory") == "path/to/directory/");
} // normalize_path

TEST_CASE("sanitize_filename")
{
    auto sanitize_filename = [](string s) {
        crow::utility::sanitize_filename(s);
        return s;
    };
    CHECK(sanitize_filename("abc/def") == "abc/def");
    CHECK(sanitize_filename("abc/../def") == "abc/_/def");
    CHECK(sanitize_filename("abc/..\\..\\..//.../def") == "abc/_\\_\\_//_./def");
    CHECK(sanitize_filename("abc/..../def") == "abc/_../def");
    CHECK(sanitize_filename("abc/x../def") == "abc/x../def");
    CHECK(sanitize_filename("../etc/passwd") == "_/etc/passwd");
    CHECK(sanitize_filename("abc/AUX") == "abc/_");
    CHECK(sanitize_filename("abc/AUX/foo") == "abc/_/foo");
    CHECK(sanitize_filename("abc/AUX:") == "abc/__");
    CHECK(sanitize_filename("abc/AUXxy") == "abc/AUXxy");
    CHECK(sanitize_filename("abc/AUX.xy") == "abc/_.xy");
    CHECK(sanitize_filename("abc/NUL") == "abc/_");
    CHECK(sanitize_filename("abc/NU") == "abc/NU");
    CHECK(sanitize_filename("abc/NuL") == "abc/_");
    CHECK(sanitize_filename("abc/LPT1\\") == "abc/_\\");
    CHECK(sanitize_filename("abc/COM1") == "abc/_");
    CHECK(sanitize_filename("ab?<>:*|\"cd") == "ab_______cd");
    CHECK(sanitize_filename("abc/COM9") == "abc/_");
    CHECK(sanitize_filename("abc/COM") == "abc/COM");
    CHECK(sanitize_filename("abc/CON") == "abc/_");
    CHECK(sanitize_filename("/abc/") == "_abc/");
}

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

    t.expires_from_now(3 * timer.get_tick_length());
    t.wait(ec);
    // we are at 3 ticks, nothing be changed yet
    CHECK(!ec);
    CHECK(a == false);
    CHECK(b == false);
    t.expires_from_now(3 * timer.get_tick_length());
    t.wait(ec);
    // we are at 3+3 = 6 ticks, so first task_timer handler should have runned
    CHECK(!ec);
    CHECK(a == true);
    CHECK(b == false);

    t.expires_from_now(5 * timer.get_tick_length());
    t.wait(ec);
    //we are at 3+3 +5 = 11 ticks, both task_timer handlers shoudl have run now
    CHECK(!ec);
    CHECK(a == true);
    CHECK(b == true);

    io_context.stop();
    io_thread.join();
} // task_timer


TEST_CASE("trim")
{
    CHECK(utility::trim("") == "");
    CHECK(utility::trim("0") == "0");
    CHECK(utility::trim(" a") == "a");
    CHECK(utility::trim("b ") == "b");
    CHECK(utility::trim(" c ") == "c");
    CHECK(utility::trim(" a b ") == "a b");
    CHECK(utility::trim("   ") == "");
}

TEST_CASE("string_equals")
{
    CHECK(utility::string_equals("a", "aa") == false);
    CHECK(utility::string_equals("a", "b") == false);
    CHECK(utility::string_equals("", "") == true);
    CHECK(utility::string_equals("abc", "abc") == true);
    CHECK(utility::string_equals("ABC", "abc") == true);

    CHECK(utility::string_equals("a", "aa", true) == false);
    CHECK(utility::string_equals("a", "b", true) == false);
    CHECK(utility::string_equals("", "", true) == true);
    CHECK(utility::string_equals("abc", "abc", true) == true);
    CHECK(utility::string_equals("ABC", "abc", true) == false);
}

TEST_CASE("lexical_cast")
{
    CHECK(utility::lexical_cast<int>(4) == 4);
    CHECK(utility::lexical_cast<double>(4) == 4.0);
    CHECK(utility::lexical_cast<int>("5") == 5);
    CHECK(utility::lexical_cast<string>(4) == "4");
    CHECK(utility::lexical_cast<float>("10", 2) == 10.0f);
}

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

    asio::io_service is;

    auto make_request = [&](const std::string& rq) {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));
        c.send(asio::buffer(rq));
        std::string fullString{};
        asio::error_code error;
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
