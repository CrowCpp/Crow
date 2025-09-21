#include "catch2/catch_all.hpp"

#include "crow.h"

using namespace std;
using namespace crow;

TEST_CASE("template_basic", "[mustache]")
{
    auto t = crow::mustache::compile(R"---(attack of {{name}})---");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes" == result);
} // template_basic

TEST_CASE("template_true_tag", "[mustache]")
{
    auto t = crow::mustache::compile(R"---({{true_value}})---");
    crow::mustache::context ctx;
    ctx["true_value"] = true;
    auto result = t.render_string(ctx);
    CHECK("true" == result);
} // template_true_tag

TEST_CASE("template_false_tag", "[mustache]")
{
    auto t = crow::mustache::compile(R"---({{false_value}})---");
    crow::mustache::context ctx;
    ctx["false_value"] = false;
    auto result = t.render_string(ctx);
    CHECK("false" == result);
} // template_false_tag

TEST_CASE("template_int", "[mustache]")
{
    auto t = crow::mustache::compile(R"---(Int: {{int_val}})---");
    crow::mustache::context ctx;
    ctx["int_val"] = 42;
    auto result = t.render_string(ctx);
    CHECK("Int: 42" == result);
} // template_int

TEST_CASE("template_double", "[mustache]")
{
    auto t = crow::mustache::compile(R"---(Double: {{double_val}})---");
    crow::mustache::context ctx;
    ctx["double_val"] = 3.14159;
    auto result = t.render_string(ctx);
    auto numeric_part = result.substr(result.find("3."));
    CHECK(Catch::Approx(std::stod(numeric_part)) == 3.14159);
} // template_double

TEST_CASE("template_nested_keys")
{
    auto t = crow::mustache::compile(R"---(Name: {{user.name}}, City: {{user.address.city}})---");
    crow::mustache::context ctx;    
    ctx["user"]["name"] = "Winston Smith";
    ctx["user"]["address"]["city"] = "Airstrip One";
    auto result = t.render_string(ctx);
    CHECK(result == "Name: Winston Smith, City: Airstrip One");
} // template_nested_keys

TEST_CASE("template_function", "[mustache]")
{
    auto t = crow::mustache::compile("attack of {{func}}");
    crow::mustache::context ctx;
    ctx["name"] = "killer tomatoes";
    ctx["func"] = std::function<std::string(std::string)>([&](std::string) {
        return std::string("{{name}}, IN SPACE!");
    });
    auto result = t.render_string(ctx);
    CHECK("attack of killer tomatoes, IN SPACE!" == result);
} // template_function

TEST_CASE("template_function_empty", "[mustache]")
{
    auto t = crow::mustache::compile(R"---(Message: {{func}})---");
    crow::mustache::context ctx;
    ctx["func"] = std::function<std::string(std::string)>([](std::string){ return ""; });
    auto result_empty = t.render_string(ctx);
    CHECK(result_empty == "Message: ");
} // template_function_empty

TEST_CASE("template_list", "[mustache]")
{
    auto t = mustache::compile(R"---(
        Items:
        {{#Items}}
        - {{name}} ({{quantity}})
        {{/Items}}
    )---");
    mustache::context ctx;
    ctx["items"] = json::wvalue::list({
        {{"name", "Apples"}, {"quantity", 5}},
        {{"name", "Bananas"}, {"quantity", 3}},
        {{"name", "Cherries"}, {"quantity", 7}}
    });
    auto result = t.render_string(ctx);
    std::string expected = R"---(
        Items:
        - Apples (5)
        - Bananas (3)
        - Cherries (7)
    )---";
} // template_list

TEST_CASE("template_html_esacpe", "[mustache]")
{
    auto t = crow::mustache::compile(R"---({{text}})---");
    crow::mustache::context ctx;
    ctx["text"] = "5 > 3 & 2 < 4";
    auto result = t.render_string(ctx);
    CHECK("5 &gt; 3 &amp; 2 &lt; 4" == result);
} // template_html_escape

TEST_CASE("template_inverse_else_block", "[mustache]")
{
    auto t = crow::mustache::compile(R"---({{^characters}}No characters found.{{/characters}})---");
    crow::mustache::context ctx_empty;
    ctx_empty["characters"] = crow::json::wvalue::list();
    auto result_empty = t.render_string(ctx_empty);
    crow::mustache::context ctx_nonempty;
    ctx_nonempty["characters"] = crow::json::wvalue::list();
    ctx_nonempty["characters"][0] = "Winston Smith";    
    auto result_nonempty = t.render_string(ctx_nonempty);
    CHECK(result_nonempty == "");   
} // template_inverse_else_block

TEST_CASE("template_partial", "[mustache]") 
{
    mustache::set_base(".");
    std::ofstream("partial.mustache") << "tomatoes";
    std::ofstream("main.mustache") << R"---(killer {{> partial.mustache}})---";
    auto main_template = mustache::load("main.mustache");
    mustache::context ctx;
    auto result = main_template.render_string(ctx);
    CHECK(result == "killer tomatoes");
    std::remove("partial.mustache");
    std::remove("main.mustache");
} // template_partial

TEST_CASE("template_load", "[mustache]")
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

TEST_CASE("template_custom_loader", "[mustache]")
{
    crow::mustache::set_loader([](std::string filename) -> std::string {
        (void)filename;
        return "War is {{war}}";
    });
    auto t = crow::mustache::load("nonexistant_file.mustache");
    crow::mustache::context ctx;
    ctx["war"] = "peace";
    auto result = t.render_string(ctx);
    CHECK(result == "War is peace");
    crow::mustache::set_loader(crow::mustache::default_loader);
} // template_custom_loader

TEST_CASE("template_custom_loader_with_partial", "[mustache]")
{
    crow::mustache::set_loader([](std::string filename) -> std::string {
        if (filename == "main.mustache")
            return "Big Brother says: {{> partial.mustache}}";
        else if (filename == "partial.mustache")
            return "War is {{war}}";
        return "";
    });
    auto t = crow::mustache::load("main.mustache");
    crow::mustache::context ctx;
    ctx["war"] = "peace";
    auto result = t.render_string(ctx);
    CHECK(result == "Big Brother says: War is peace");
    crow::mustache::set_loader(crow::mustache::default_loader);
} // template_custom_loader_with_partial

TEST_CASE("template_unescaped_triple_mustache", "[mustache]")
{
    auto t = crow::mustache::compile(R"---(
        Escaped: {{escaped}}
        Unescaped: {{& unescaped}}
        Triple: {{{triple}}}
    )---");
    crow::mustache::context ctx;
    ctx["escaped"] = "<b>Bold</b>";
    ctx["unescaped"] = "<i>Italic</i>";
    ctx["triple"] = "<u>Underline</u>";
    auto result = t.render_string(ctx);
    CHECK(result.find("&lt;b&gt;Bold&lt;&#x2F;b&gt;") != std::string::npos);
    // Unescaped tag should not escape content
    CHECK(result.find("<i>Italic</i>") != std::string::npos);
    // Triple mustache should not escape content
    CHECK(result.find("<u>Underline</u>") != std::string::npos);
} // template_unescaped_triple_mustache

TEST_CASE("template_multiline_indentation", "[mustache]") 
{
    auto t = crow::mustache::compile(R"---(
        Characters:
        {{#characters}}
          - Name: {{name}}
            Age: {{age}}
        {{/characters}}
        End of list.
    )---");
    crow::mustache::context ctx;
    ctx["characters"] = crow::json::wvalue::list();

    crow::mustache::context char1;
    char1["name"] = "Winston Smith";
    char1["age"] = 39;
    ctx["characters"][0] = std::move(char1);

    crow::mustache::context char2;
    char2["name"] = "Julia";
    char2["age"] = 26;
    ctx["characters"][1] = std::move(char2);

    auto result = t.render_string(ctx);
    // Check that each character block is properly indented
    CHECK(result.find("  - Name: Winston Smith") != std::string::npos);
    CHECK(result.find("    Age: 39") != std::string::npos);
    CHECK(result.find("  - Name: Julia") != std::string::npos);
    CHECK(result.find("    Age: 26") != std::string::npos);
    CHECK(result.find("End of list.") != std::string::npos);
} // template_multiline_indentation

TEST_CASE("TemlateRouting", "[mustache]")
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
