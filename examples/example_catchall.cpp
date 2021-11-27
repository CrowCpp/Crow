#include <crow.h>



int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello";
    });

    //Setting a custom route for any URL that isn't defined, instead of a simple 404.
    CROW_CATCHALL_ROUTE(app)
    ([](crow::response& res) {
        if (res.code == 404)
        {
            res.body = "The URL does not seem to be correct.";
        }
        else if (res.code == 405)
        {
            res.body = "The HTTP method does not seem to be correct.";
        }
        res.end();
    });

    app.port(18080).run();
}
