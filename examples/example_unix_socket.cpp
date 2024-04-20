#include "crow.h"

#include <sys/stat.h>

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });

    std::string unix_path = "example.sock";
    unlink(unix_path.c_str());
    app.unix_path(unix_path).run();

}
