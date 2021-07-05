#define CROW_MAIN
#include "crow.h"

int main()
{
    crow::SimpleApp app;

    crow::Blueprint bp("bp_prefix", "cstat");

    CROW_BP_ROUTE(app, bp, "/")
    ([]() {
        return "Hello world!";
    });

    app.register_blueprint(bp);

    app.port(18080).run();
}
