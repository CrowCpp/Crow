#include "crow.h"

struct RequestLogger
{
    struct context
    {};

    // This method is run before handling the request
    void before_handle(crow::request& req, crow::response& /*res*/, context& /*ctx*/)
    {
        CROW_LOG_INFO << "Request to:" + req.url;
    }

    // This method is run after handling the request
    void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
    {}
};

// Per handler middleware has to extend ILocalMiddleware
// It is called only if enabled
struct SecretContentGuard : crow::ILocalMiddleware
{
    struct context
    {};

    void before_handle(crow::request& /*req*/, crow::response& res, context& /*ctx*/)
    {
        // A request can be aborted prematurely
        res.write("SECRET!");
        res.code = 403;
        res.end();
    }

    void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
    {}
};

struct RequestAppend : crow::ILocalMiddleware
{
    // Values from this context can be accessed from handlers
    struct context
    {
        std::string message;
    };

    void before_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
    {}

    void after_handle(crow::request& /*req*/, crow::response& res, context& ctx)
    {
        // The response can be modified
        res.write(" + (" + ctx.message + ")");
    }
};

int main()
{
    // ALL middleware (including per handler) is listed
    crow::App<RequestLogger, SecretContentGuard, RequestAppend> app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });

    CROW_ROUTE(app, "/secret")
      // Enable SecretContentGuard for this handler
      .CROW_MIDDLEWARES(app, SecretContentGuard)([]() {
          return "";
      });

    crow::Blueprint bp("bp", "c", "c");
    // Register middleware on all routes on a specific blueprint
    // This also applies to sub blueprints
    bp.CROW_MIDDLEWARES(app, RequestAppend);

    CROW_BP_ROUTE(bp, "/")
    ([&](const crow::request& req) {
        // Get RequestAppends context
        auto& ctx = app.get_context<RequestAppend>(req);
        ctx.message = "World";
        return "Hello:";
    });
    app.register_blueprint(bp);

    app.port(18080).run();
    return 0;
}
