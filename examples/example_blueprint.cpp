#include "crow.h"

int main()
{
    crow::SimpleApp app;

    crow::Blueprint bp("bp_prefix", "cstat", "ctemplate");


    crow::Blueprint sub_bp("bp2", "csstat", "cstemplate");

    CROW_BP_ROUTE(sub_bp, "/")
    ([]() {
        return "Hello world!";
    });

    /*    CROW_BP_ROUTE(bp, "/templatt")
    ([]() {
        crow::mustache::context ctxdat;
        ctxdat["messg"] = "fifty five!!";

        auto page = crow::mustache::load("indks.html");

        return page.render(ctxdat);
    });
*/
    CROW_BP_CATCHALL_ROUTE(sub_bp)
    ([]() {
        return "WRONG!!";
    });


    bp.register_blueprint(sub_bp);
    app.register_blueprint(bp);

    app.loglevel(crow::LogLevel::Debug).port(18080).run();
}
