#include "crow.h"
#include "crow/file_body_sink.h"

#include <fstream>
#include <iterator>
#include <string>

int main()
{
    crow::SimpleApp app;
    app.max_body_size(64ull * 1024 * 1024);

    // PUT or POST the raw body; Crow writes it to disk as the bytes arrive.
    CROW_ROUTE(app, "/upload")
      .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Post)
      .body_sink(crow::FileBodySink::factory("uploads"))([](const crow::request& req) {
          auto* file = crow::FileBodySink::from(req);
          if (!file)
          {
              // No incoming body (e.g. an empty PUT): the sink never opened.
              crow::json::wvalue reply;
              reply["bytes"] = 0;
              reply["preview"] = "";
              return crow::response(reply);
          }

          std::ifstream in(file->path(), std::ios::binary);
          const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

          crow::json::wvalue reply;
          reply["bytes"] = contents.size();
          reply["preview"] = contents.substr(0, 32);
          return crow::response(reply);
      });

    app.port(18080).multithreaded().run();
}
