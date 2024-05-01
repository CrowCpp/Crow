#include "http.h"

class ExampleLogHandler : public http::ILogHandler
{
public:
    void log(std::string /*message*/, http::LogLevel /*level*/) override
    {
        //            cerr << "ExampleLogHandler -> " << message;
    }
};

int main()
{
    http::SimpleApp app;

    CROW_ROUTE(app, "/")
      .name("hello")([] {
          return "Hello World!";
      });

    CROW_ROUTE(app, "/about")
    ([]() {
        return "About Crow example.";
    });

    // simple json response
    CROW_ROUTE(app, "/json")
    ([] {
        http::json::wvalue x({{"message", "Hello, World!"}});
        x["message2"] = "Hello, World.. Again!";
        return x;
    });

    CROW_ROUTE(app, "/json-initializer-list-constructor")
    ([] {
        http::json::wvalue r({
          {"first", "Hello world!"},                     /* stores a char const* hence a json::type::String */
          {"second", std::string("How are you today?")}, /* stores a std::string hence a json::type::String. */
          {"third", 54},                                 /* stores an int (as 54 is an int literal) hence a std::int64_t. */
          {"fourth", (int64_t)54l},                               /* stores a long (as 54l is a long literal) hence a std::int64_t. */
          {"fifth", 54u},                                /* stores an unsigned int (as 54u is a unsigned int literal) hence a std::uint64_t. */
          {"sixth", (uint64_t)54ul},                               /* stores an unsigned long (as 54ul is an unsigned long literal) hence a std::uint64_t. */
          {"seventh", 2.f},                              /* stores a float (as 2.f is a float literal) hence a double. */
          {"eighth", 2.},                                /* stores a double (as 2. is a double literal) hence a double. */
          {"ninth", nullptr},                            /* stores a std::nullptr hence json::type::Null . */
          {"tenth", true}                                /* stores a bool hence json::type::True . */
        });
        return r;
    });

    // json list response
    CROW_ROUTE(app, "/json_list")
    ([] {
        http::json::wvalue x(http::json::wvalue::list({1, 2, 3}));
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

    // example which uses only response as a paramter without
    // request being a parameter.
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
      .methods("POST"_method)([](const http::request& req) {
          auto x = http::json::load(req.body);
          if (!x)
              return http::response(400);
          int sum = x["a"].i() + x["b"].i();
          std::ostringstream os;
          os << sum;
          return http::response{os.str()};
      });

    CROW_ROUTE(app, "/params")
    ([](const http::request& req) {
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
      .server_name("CrowCpp")
      .multithreaded()
      .run();
}
