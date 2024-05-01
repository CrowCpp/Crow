#include "http.h"

class ExampleLogHandler : public http::ILogHandler
{
public:
    void log(std::string message, http::LogLevel level) override
    {
        //            cerr << "ExampleLogHandler -> " << message;
    }
};

struct ExampleMiddleware
{
    std::string message;

    ExampleMiddleware():
      message("foo")
    {
    }

    void setMessage(const std::string& newMsg)
    {
        message = newMsg;
    }

    struct context
    {};

    void before_handle(http::request& req, http::response& res, context& ctx)
    {
        CROW_LOG_DEBUG << " - MESSAGE: " << message;
    }

    void after_handle(http::request& req, http::response& res, context& ctx)
    {
        // no-op
    }
};

int main()
{
    http::App<ExampleMiddleware> app;

    app.get_middleware<ExampleMiddleware>().setMessage("hello");

    CROW_ROUTE(app, "/")
      .name("hello")([] {
          return "Hello World!";
      });

    CROW_ROUTE(app, "/about")
    ([]() {
        return "About Crow example.";
    });

    // a request to /path should be forwarded to /path/
    CROW_ROUTE(app, "/path/")
    ([]() {
        return "Trailing slash test case..";
    });

    // simple json response
    CROW_ROUTE(app, "/json")
    ([] {
        http::json::wvalue x;
        x["message"] = "Hello, World!";
        return x;
    });

    CROW_ROUTE(app, "/hello/<int>")
    ([](int count) {
        if (count > 100)
            return http::response(400);
        std::ostringstream os;
        os << count << " bottles of beer!";
        return http::response(os.str());
    });

    CROW_ROUTE(app, "/add/<int>/<int>")
    ([](http::response& res, int a, int b) {
        std::ostringstream os;
        os << a + b;
        res.write(os.str());
        res.end();
    });

    // Compile error with message "Handler type is mismatched with URL paramters"
    //CROW_ROUTE(app,"/another/<int>")
    //([](int a, int b){
    //return crow::response(500);
    //});

    // more json example
    CROW_ROUTE(app, "/add_json")
      .methods(http::HTTPMethod::Post)([](const http::request& req) {
          auto x = http::json::load(req.body);
          if (!x)
              return http::response(400);
          auto sum = x["a"].i() + x["b"].i();
          std::ostringstream os;
          os << sum;
          return http::response{os.str()};
      });

    app.route_dynamic("/params")([](const http::request& req) {
        std::ostringstream os;
        os << "Params: " << req.url_params << "\n\n";
        os << "The key 'foo' was " << (req.url_params.get("foo") == nullptr ? "not " : "") << "found.\n";
        if (req.url_params.get("pew") != nullptr)
        {
            double countD = http::utility::lexical_cast<double>(req.url_params.get("pew"));
            os << "The value of 'pew' is " << countD << '\n';
        }
        auto count = req.url_params.get_list("count");
        os << "The key 'count' contains " << count.size() << " value(s).\n";
        for (const auto& countVal : count)
        {
            os << " - " << countVal << '\n';
        }
        return http::response{os.str()};
    });

    // ignore all log
    http::logger::setLogLevel(http::LogLevel::Debug);
    //crow::logger::setHandler(std::make_shared<ExampleLogHandler>());

    app.port(18080)
      .multithreaded()
      .run();
}
