#include "http.h"
#include "http/middlewares/cookie_parser.h"

int main()
{
    // Include CookieParser middleware
    http::App<http::CookieParser> app;

    CROW_ROUTE(app, "/read")
    ([&](const http::request& req) {
        auto& ctx = app.get_context<http::CookieParser>(req);
        // Read cookies with get_cookie
        auto value = ctx.get_cookie("key");
        return "value: " + value;
    });

    CROW_ROUTE(app, "/write")
    ([&](const http::request& req) {
        auto& ctx = app.get_context<http::CookieParser>(req);
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
