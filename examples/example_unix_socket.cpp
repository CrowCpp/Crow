#include "crow.h"

#include <sys/stat.h>

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });

    std::string local_socket_path = "example.sock";
    unlink(local_socket_path.c_str());
    app.local_socket_path(local_socket_path).run();

}
