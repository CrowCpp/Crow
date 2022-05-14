// Test of a project containing more than 1 source file which includes Crow headers.
// The test succeeds if the project is linked successfully.
#include "crow.h"

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });

    app.port(18080).run();
}
