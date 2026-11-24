#include "catch2/catch_all.hpp"

#include "crow.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

using namespace std;
using namespace crow;

namespace
{
    struct loader_reset
    {
        ~loader_reset()
        {
            mustache::set_loader(mustache::default_loader);
        }
    };

    std::string load_through(int entry, const std::string& filename)
    {
        switch (entry)
        {
            case 0: return mustache::load_text(filename);
            case 1: return mustache::load_text_unsafe(filename);
            case 2: return mustache::load(filename).render_string();
            default: return mustache::load_unsafe(filename).render_string();
        }
    }
} // namespace

TEST_CASE("template_loader_retains_in_flight_callable", "[mustache][loader]")
{
    loader_reset reset;
    const int entry = GENERATE(0, 1, 2, 3);
    CAPTURE(entry);
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> previous_loader = lifetime;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    auto started = entered.get_future();
    mustache::set_loader([lifetime, &entered, released](std::string) {
        // Do not access the callable's captures after announcing entry.
        auto done = released;
        entered.set_value();
        done.wait();
        return "old";
    });
    lifetime.reset();
    std::string result;
    std::exception_ptr error;
    std::thread reader([&] {
        try
        {
            result = load_through(entry, "page");
        }
        catch (...)
        {
            error = std::current_exception();
        }
    });
    const bool did_start = started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    std::promise<void> replaced;
    auto replacement = replaced.get_future();
    std::thread writer([&] {
        mustache::set_loader([](std::string) {
            return "new";
        });
        replaced.set_value();
    });
    const bool did_replace = replacement.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const bool retained = !previous_loader.expired();
    release.set_value();
    reader.join();
    writer.join();
    CHECK(did_start);
    CHECK(did_replace);
    CHECK(retained);
    CHECK_FALSE(error);
    CHECK(result == "old");
    CHECK(previous_loader.expired());
    CHECK(load_through(entry, "page") == "new");
}


TEST_CASE("template_loader_preserves_mutable_state", "[mustache][loader]")
{
    loader_reset reset;
    mustache::set_loader([count = 0](std::string) mutable {
        return std::to_string(++count);
    });
    CHECK(mustache::load_text("page") == "1");
    CHECK(mustache::load_text_unsafe("page") == "2");
    CHECK(mustache::load("page").render_string() == "3");
    CHECK(mustache::load_unsafe("page").render_string() == "4");
}

TEST_CASE("template_loader_callback_can_replace_and_load", "[mustache][loader]")
{
    loader_reset reset;
    const int entry = GENERATE(0, 1, 2, 3);
    CAPTURE(entry);
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> previous_loader = lifetime;
    bool retained = false;
    mustache::set_loader([lifetime, &retained, &previous_loader, entry](std::string) {
        auto* retained_result = &retained;
        auto* previous = &previous_loader;
        const int nested_entry = entry;
        mustache::set_loader([](std::string) {
            return "nested";
        });
        *retained_result = !previous->expired();
        return load_through(nested_entry, "page");
    });
    lifetime.reset();
    CHECK(load_through(entry, "page") == "nested");
    CHECK(retained);
    CHECK(previous_loader.expired());
}

TEST_CASE("template_loader_destructor_can_replace_and_load", "[mustache][loader]")
{
    loader_reset reset;
    std::string observed;
    auto lifetime = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        observed = mustache::load_text("page");
        mustache::set_loader([](std::string) {
            return "reentered";
        });
    });
    mustache::set_loader([lifetime](std::string) {
        return "old";
    });
    lifetime.reset();
    mustache::set_loader([](std::string) {
        return "replacement";
    });
    CHECK(observed == "replacement");
    CHECK(mustache::load_text("page") == "reentered");
}

TEST_CASE("template_loader_preserves_errors_and_empty_results", "[mustache][loader]")
{
    loader_reset reset;
    const int entry = GENERATE(0, 1, 2, 3);
    CAPTURE(entry);
    mustache::set_loader([](std::string) -> std::string {
        throw std::runtime_error("loader error");
    });
    CHECK_THROWS_WITH(load_through(entry, "page"), "loader error");
    mustache::set_loader({});
    CHECK_THROWS_AS(load_through(entry, "page"), std::bad_function_call);
    mustache::set_loader([](std::string) {
        return "";
    });
    CHECK(load_through(entry, "page").empty());
    mustache::set_loader([](std::string) {
        return "recovered";
    });
    CHECK(load_through(entry, "page") == "recovered");
}

TEST_CASE("template_loader_preserves_paths_and_rendering", "[mustache][loader]")
{
    loader_reset reset;
    const int entry = GENERATE(0, 1, 2, 3);
    CAPTURE(entry);
    std::string observed;
    mustache::set_loader([&](std::string filename) {
        observed = filename;
        return "{{name}}";
    });
    const std::string result = load_through(entry, "bad?.mustache");
    CHECK(observed == ((entry == 0 || entry == 2) ? "bad_.mustache" : "bad?.mustache"));
    CHECK(result == ((entry < 2) ? "{{name}}" : ""));
    mustache::set_loader([](std::string filename) {
        return filename == "main" ? "Hello {{name}} {{>partial}}" : "{{name}}!";
    });
    mustache::context ctx;
    ctx["name"] = "Crow";
    CHECK(mustache::load("main").render_string(ctx) == "Hello Crow Crow!");
    CHECK(mustache::load_unsafe("main").render_string(ctx) == "Hello Crow Crow!");
}

TEST_CASE("template_loader_concurrent_replacement", "[mustache][loader]")
{
    loader_reset reset;
    std::atomic<bool> start{false};
    std::atomic<unsigned> failures{0};
    std::atomic<unsigned> calls{0};
    std::vector<std::thread> workers;
    mustache::set_loader([](std::string) {
        return "initial";
    });
    for (int writer = 0; writer < 2; ++writer)
    {
        workers.emplace_back([&, writer] {
            while (!start.load())
                std::this_thread::yield();
            for (int i = 0; i < 1000; ++i)
            {
                // Immutable state: registry safety does not serialize user code.
                auto value = std::make_shared<const std::string>(std::to_string(writer));
                mustache::set_loader([value](std::string) {
                    return *value;
                });
            }
        });
    }
    for (int entry = 0; entry < 4; ++entry)
    {
        workers.emplace_back([&, entry] {
            while (!start.load())
                std::this_thread::yield();
            for (int i = 0; i < 1000; ++i)
            {
                try
                {
                    auto value = load_through(entry, "page");
                    if (value != "initial" && value != "0" && value != "1")
                        ++failures;
                }
                catch (...)
                {
                    ++failures;
                }
                ++calls;
            }
        });
    }
    start.store(true);
    for (auto& worker : workers)
        worker.join();
    CHECK(calls.load() == 4000);
    CHECK(failures.load() == 0);
}

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
    std::remove("test.mustache");
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
