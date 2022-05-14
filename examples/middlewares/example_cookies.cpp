#include "crow.h"
#include "crow/middlewares/cookie_parser.h"

int main()
{
    // Include CookieParser middleware
    crow::App<crow::CookieParser> app;

    CROW_ROUTE(app, "/read")
    ([&](const crow::request& req) {
        auto& ctx = app.get_context<crow::CookieParser>(req);
        // Read cookies with get_cookie
        auto value = ctx.get_cookie("key");
        return "value: " + value;
    });

    CROW_ROUTE(app, "/write")
    ([&](const crow::request& req) {
        auto& ctx = app.get_context<crow::CookieParser>(req);
        // Store cookies with set_cookie
        ctx.set_cookie("key", "word")
          // configure additional parameters
          .path("/")
          .max_age(120)
          .httponly();
        return "ok!";
    });

    app.port(18080).run();

    return 0;
}
