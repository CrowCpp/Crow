#include "crow.h"

#include <fstream>
#include <iterator>
#include <string>

int main()
{
    crow::SimpleApp app;
    app.body_file_directory("uploads").max_body_size(64ull * 1024 * 1024);

    // PUT or POST the raw body; Crow writes it to disk as the bytes arrive.
    CROW_ROUTE(app, "/upload")
      .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Post)
      .body_file()([](const crow::request& req) {
          if (!req.has_body_file())
              return crow::response(500);

          std::ifstream in(req.body_file_path, std::ios::binary);
          const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

          crow::json::wvalue reply;
          reply["bytes"] = contents.size();
          reply["preview"] = contents.substr(0, 32);
          return crow::response(reply);
      });

    app.port(18080).multithreaded().run();
}
