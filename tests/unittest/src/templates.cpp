#include "unittest.h"

TEST_CASE("template_basic")
{
    auto t = crow::mustache::compile(R"---(attack of {{name}})---");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes" == result);
} // template_basic

TEST_CASE("template_function")
{
    auto t = crow::mustache::compile("attack of {{func}}");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    ctx["func"] = [&](std::string) {
        return std::string("{{name}}, IN SPACE!");
    };
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

        app.handle(req, res);

        CHECK("attack of killer tomatoes" == res.body);
        CHECK("text/html" == crow::get_header_value(res.headers, "Content-Type"));
    }
} // PathRouting
