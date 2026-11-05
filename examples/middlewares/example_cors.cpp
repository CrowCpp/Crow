#include "crow.h"
#include "crow/middlewares/cors.h"

// Warning!
// If you want to use CORS with OPTIONS cache on browser requests,
// be sure to specify each headers you use, please do not use "*"
// else otherwise the browser will ignore you
// Example:
//    .headers("Origin", "Content-Type", "Accept", *Your-Headers*)
//    .max_age(5);

int main()
{
    // Enable CORS
    crow::App<crow::CORSHandler> app;

    // Customize CORS
    auto& cors = app.get_middleware<crow::CORSHandler>();

    // clang-format off
    cors
      .global()
        .headers("X-Custom-Header", "Upgrade-Insecure-Requests")
        .methods("POST"_method, "GET"_method)
      .prefix("/cors")
        .origin("example.com")
      .prefix("/nocors")
        .ignore();
    // clang-format on

    CROW_ROUTE(app, "/")
    ([]() {
        return "Check Access-Control-Allow-Methods header";
    });

    CROW_ROUTE(app, "/cors")
    ([]() {
        return "Check Access-Control-Allow-Origin header";
    });

    app.port(18080).run();

    return 0;
}
