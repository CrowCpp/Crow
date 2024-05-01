// Testing whether crow routes can be defined in an external function.
#include "http.h"

void define_endpoints(http::SimpleApp& app)
{
    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });
    CROW_WEBSOCKET_ROUTE(app, "/ws")
      .onaccept([&](const http::request&, void**) {
          return true;
      })
      .onopen([](http::websocket::connection&) {})
      .onclose([](http::websocket::connection&, const std::string&) {});
}

int main()
{
    http::SimpleApp app;

    define_endpoints(app);

    app.port(18080).run();
}
