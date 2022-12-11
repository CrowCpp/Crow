// Testing whether crow routes can be defined in an external function.
#include "crow.h"

void define_endpoints(crow::SimpleApp& app)
{
    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });
    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onaccept([&](const crow::request&, void**) {
          return true;
      })
      .onopen([](crow::websocket::connection&) {})
      .onclose([](crow::websocket::connection&, const std::string&) {});
}

int main()
{
    crow::SimpleApp app;

    define_endpoints(app);

    app.port(18080).run();
}
