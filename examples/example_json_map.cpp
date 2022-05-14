#define CROW_JSON_USE_MAP
#include "crow.h"

int main()
{
    crow::SimpleApp app;

    // simple json response using a map
    // To see it in action enter {ip}:18080/json
    // it shoud show amessage before zmessage despite adding zmessage first.
    CROW_ROUTE(app, "/json")
    ([] {
        crow::json::wvalue x({{"zmessage", "Hello, World!"},
                              {"amessage", "Hello, World2!"}});
        return x;
    });

    app.port(18080)
      .multithreaded()
      .run();
}
