#include "unittest.h"

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

        app.handle(req, res);

        CHECK(200 == res.code);
    }
    {
        request req;
        response res;

        req.url = "/file/";

        app.handle(req, res);
        CHECK(404 == res.code);
    }
    {
        request req;
        response res;

        req.url = "/path";

        app.handle(req, res);
        CHECK(404 != res.code);
    }
    {
        request req;
        response res;

        req.url = "/path/";

        app.handle(req, res);
        CHECK(200 == res.code);
    }
} // PathRouting

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

        app.handle(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/0/1001999";

        app.handle(req, res);

        CHECK(200 == res.code);

        CHECK(1001999 == B);
    }

    {
        request req;
        response res;

        req.url = "/1/-100/1999";

        app.handle(req, res);

        CHECK(200 == res.code);

        CHECK(-100 == A);
        CHECK(1999 == B);
    }
    {
        request req;
        response res;

        req.url = "/4/5000/3/-2.71828/hellhere";
        req.add_header("TestHeader", "Value");

        app.handle(req, res);

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

        app.handle(req, res);

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
        app.handle(req, res);
        CHECK(x == 2);
    }
    {
        request req;
        response res;
        req.url = "/set_int/42";
        app.handle(req, res);
        CHECK(x == 42);
    }
    {
        request req;
        response res;
        req.url = "/set5";
        app.handle(req, res);
        CHECK(x == 5);
    }
    {
        request req;
        response res;
        req.url = "/set4";
        app.handle(req, res);
        CHECK(x == 4);
    }
} // route_dynamic

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

        app.handle(req, res);

        CHECK(200 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/another_place";

        app.handle(req, res);

        CHECK(404 == res.code);
        CHECK("!place" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/place";

        app2.handle(req, res);

        CHECK(200 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/another_place";

        app2.handle(req, res);

        CHECK(404 == res.code);
    }
} // catchall
