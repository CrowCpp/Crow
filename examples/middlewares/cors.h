#include "crow.h"
#include "crow/middlewares/cors.h"


int main()
{
    // Enable CORS
    crow::App<crow::CORSHandler> app;

    // Customize CORS
    auto& cors = app.get_middleware<crow::CORSHandler>();
    // Default rules
    cors.global()
      .methods("POST"_method, "GET"_method);
    // Rules for prefix /cors
    cors.prefix("/cors")
      .origin("example.com");


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
