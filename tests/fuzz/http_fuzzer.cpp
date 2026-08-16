#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include "crow.h"

struct DummyHandler {
    void handle_url() {}
    void handle_header() {}
    void handle() {}
    size_t stream_threshold() { return 1024*1024; }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static crow::SimpleApp app;
    static bool initialized = false;
    if (!initialized) {
        CROW_ROUTE(app, "/test/<string>/<int>")
          ([](const crow::request& req, std::string a, int b)
           {
             return "OK";
           });
        CROW_ROUTE(app, "/json")
          ([](const crow::request& req)
           {
             auto j = crow::json::load(req.body);
             if (j) return "JSON";
             return "NOT JSON";
           });
        crow::logger::setLogLevel(crow::LogLevel::CRITICAL);
        initialized = true;
    }

    DummyHandler dummy;
    crow::HTTPParser<DummyHandler> parser(&dummy);
    
    if (parser.feed(reinterpret_cast<const char*>(data), size)) {
        parser.done();
        crow::response res;
        app.handle_full(parser.req, res);
    }

    return 0;
}
