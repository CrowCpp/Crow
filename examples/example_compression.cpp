#include "http.h"
#include "http/compression.h"

int main()
{
    http::SimpleApp app;
    //crow::App<crow::CompressionGzip> app;

    CROW_ROUTE(app, "/hello")
    ([&](const http::request&, http::response& res) {
        res.compressed = false;

        res.body = "Hello World! This is uncompressed!";
        res.end();
    });

    CROW_ROUTE(app, "/hello_compressed")
    ([]() {
        return "Hello World! This is compressed by default!";
    });


    app.port(18080)
      .use_compression(http::compression::algorithm::DEFLATE)
      //.use_compression(crow::compression::algorithm::GZIP)
      .loglevel(http::LogLevel::Debug)
      .multithreaded()
      .run();
}
