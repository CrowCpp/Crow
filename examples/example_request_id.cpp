#include "crow.h"
#include "crow/middlewares/request_id.h"

int main()
{
    crow::App<crow::middleware::RequestId> app;

    CROW_ROUTE(app, "/")
    .name("hello")([] {
        return "Hello World!";
    });

    CROW_ROUTE(app, "/about")
    ([]() {
        return "About Crow example with Request ID middleware.";
    });

    // simple json response
    CROW_ROUTE(app, "/json")
    ([] {
        crow::json::wvalue x({{"message", "Hello, World!"}});
        x["message2"] = "Hello, World.. Again!";
        return x;
    });

    // Health endpoint with request_id
    CROW_ROUTE(app, "/health")
    ([&](const crow::request& req) {
        auto& ctx = app.get_context<crow::middleware::RequestId>(req);
        crow::json::wvalue x;
        x["status"] = "ok";
        x["request_id"] = ctx.request_id;
        return x;
    });

    // enables all log
    app.loglevel(crow::LogLevel::Debug);

    app.port(18080)
      .multithreaded()
      .run();
}
