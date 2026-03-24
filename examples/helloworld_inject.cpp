#include "crow.h"

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([](const crow::request &req, crow::response& res) {
        res.write("Hello, world!");
        res.set_header("X-Custom", "safe\r\nInjected: yes");
        res.end();
        //return "Hello, world!";
    });

    app.port(18080).run();
}
