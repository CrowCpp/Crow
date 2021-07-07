#define CROW_MAIN
#include "crow.h"

int main()
{
    crow::SimpleApp app;

    crow::Blueprint bp("bp_prefix", "cstat", "ctemplate");

    CROW_BP_ROUTE(app, bp, "/")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(app, bp, "/templatt")
    ([]() {
        crow::mustache::context ctxdat;
        ctxdat["messg"] = "fifty five!!";

        auto page = crow::mustache::load("indks.html");

        return page.render(ctxdat);
    });

    app.register_blueprint(bp);

    app.port(18080).run();
}
