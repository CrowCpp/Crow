#include "crow.h"

#ifdef WIN32
#include <fileapi.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")
    ([]() {
        return "Hello, world!";
    });

    std::string unix_path = "example.sock";
#ifdef WIN32
    DeleteFileA(unix_path.c_str());
#else
    unlink(unix_path.c_str());
#endif
    app.unix_path(unix_path).run();

}
