#include "unittest.h"

TEST_CASE("blueprint")
{
    SimpleApp app;
    crow::Blueprint bp("bp_prefix", "cstat", "ctemplate");
    crow::Blueprint bp_not_sub("bp_prefix_second");
    crow::Blueprint sub_bp("bp2", "csstat", "cstemplate");
    crow::Blueprint sub_sub_bp("bp3");

    CROW_BP_ROUTE(sub_bp, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(bp_not_sub, "/hello")
    ([]() {
        return "Hello world!";
    });

    CROW_BP_ROUTE(sub_sub_bp, "/hi")
    ([]() {
        return "Hi world!";
    });

    CROW_BP_CATCHALL_ROUTE(sub_bp)
    ([]() {
        return response(200, "WRONG!!");
    });

    app.register_blueprint(bp);
    app.register_blueprint(bp_not_sub);
    bp.register_blueprint(sub_bp);
    sub_bp.register_blueprint(sub_sub_bp);

    app.validate();

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/hello";

        app.handle(req, res);

        CHECK("Hello world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix_second/hello";

        app.handle(req, res);

        CHECK("Hello world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/bp3/hi";

        app.handle(req, res);

        CHECK("Hi world!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/nonexistent";

        app.handle(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix_second/nonexistent";

        app.handle(req, res);

        CHECK(404 == res.code);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/nonexistent";

        app.handle(req, res);

        CHECK(200 == res.code);
        CHECK("WRONG!!" == res.body);
    }

    {
        request req;
        response res;

        req.url = "/bp_prefix/bp2/bp3/nonexistent";

        app.handle(req, res);

        CHECK(200 == res.code);
        CHECK("WRONG!!" == res.body);
    }
} // blueprint
