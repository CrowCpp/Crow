#include "unittest.h"

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
        app.handle(req, res);

        CHECK("2" == res.body);
    }
    {
        request req;
        response res;

        req.url = "/";
        req.method = "POST"_method;
        app.handle(req, res);

        CHECK("1" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        app.handle(req, res);

        CHECK("get" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/patch_only";
        req.method = "PATCH"_method;
        app.handle(req, res);

        CHECK("patch" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/purge_only";
        req.method = "PURGE"_method;
        app.handle(req, res);

        CHECK("purge" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "POST"_method;
        app.handle(req, res);

        CHECK("get" != res.body);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "POST"_method;
        app.handle(req, res);

        CHECK(405 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/get_only";
        req.method = "HEAD"_method;
        app.handle(req, res);

        CHECK(200 == res.code);
        CHECK("" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/";
        req.method = "OPTIONS"_method;
        app.handle(req, res);

        CHECK(204 == res.code);
        CHECK("OPTIONS, HEAD, GET, POST" == res.get_header_value("Allow"));
    }

    {
        request req;
        response res;

        req.url = "/does_not_exist";
        req.method = "OPTIONS"_method;
        app.handle(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/*";
        req.method = "OPTIONS"_method;
        app.handle(req, res);

        CHECK(204 == res.code);
        CHECK("OPTIONS, HEAD, GET, POST, PATCH, PURGE" == res.get_header_value("Allow"));
    }
} // http_method

TEST_CASE("server_handling_error_request")
{
    static char buf[2048];
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    // Server<SimpleApp> server(&app, LOCALHOST_ADDRESS, 45451);
    // auto _ = async(launch::async, [&]{server.run();});
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg = "POX";
    asio::io_service is;
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg));

        try
        {
            c.receive(asio::buffer(buf, 2048));
            FAIL_CHECK();
        }
        catch (std::exception& e)
        {
            CROW_LOG_DEBUG << e.what();
        }
    }
    app.stop();
} // server_handling_error_request

TEST_CASE("server_handling_error_request_http_version")
{
    static char buf[2048];
    SimpleApp app;
    CROW_ROUTE(app, "/")
    ([] {
        return "A";
    });
    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg = "POST /\r\nContent-Length:3\r\nX-HeaderTest: 123\r\n\r\nA=B\r\n";
    asio::io_service is;
    {
        asio::ip::tcp::socket c(is);
        c.connect(asio::ip::tcp::endpoint(
          asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

        c.send(asio::buffer(sendmsg));

        try
        {
            c.receive(asio::buffer(buf, 2048));
            FAIL_CHECK();
        }
        catch (std::exception& e)
        {
            CROW_LOG_DEBUG << e.what();
        }
    }
    app.stop();
} // server_handling_error_request_http_version

TEST_CASE("undefined_status_code")
{
    SimpleApp app;
    CROW_ROUTE(app, "/get123")
    ([] {
        //this status does not exists statusCodes map defined in include/crow/http_connection.h
        const int undefinedStatusCode = 123;
        return response(undefinedStatusCode, "this should return 500");
    });

    CROW_ROUTE(app, "/get200")
    ([] {
        return response(200, "ok");
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45471).run_async();
    app.wait_for_server_start();

    asio::io_service is;
    auto sendRequestAndGetStatusCode = [&](const std::string& route) -> unsigned {
        asio::ip::tcp::socket socket(is);
        socket.connect(asio::ip::tcp::endpoint(asio::ip::address::from_string(LOCALHOST_ADDRESS), app.port()));

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

TEST_CASE("bug_quick_repeated_request")
{
    static char buf[2048];

    SimpleApp app;

    CROW_ROUTE(app, "/")
    ([&] {
        return "hello";
    });

    auto _ = app.bindaddr(LOCALHOST_ADDRESS).port(45451).run_async();
    app.wait_for_server_start();
    std::string sendmsg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    asio::io_service is;
    {
        std::vector<std::future<void>> v;
        for (int i = 0; i < 5; i++)
        {
            v.push_back(async(launch::async, [&] {
                asio::ip::tcp::socket c(is);
                c.connect(asio::ip::tcp::endpoint(
                  asio::ip::address::from_string(LOCALHOST_ADDRESS), 45451));

                for (int j = 0; j < 5; j++)
                {
                    c.send(asio::buffer(sendmsg));

                    size_t received = c.receive(asio::buffer(buf, 2048));
                    CHECK("hello" == std::string(buf + received - 5, buf + received));
                }
                c.close();
            }));
        }
    }
    app.stop();
} // bug_quick_repeated_request
