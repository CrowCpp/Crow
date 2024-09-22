#include "crow.h"

class ExampleLogHandler : public crow::ILogHandler
{
public:
    void log(std::string /*message*/, crow::LogLevel /*level*/) override
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

    void before_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
    {
        CROW_LOG_DEBUG << " - MESSAGE: " << message;
    }

    void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/)
    {
        // no-op
    }
};

int main()
{
    crow::App<ExampleMiddleware> app;

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
        crow::json::wvalue x({{"message", "Hello, World!"}});
        x["message2"] = "Hello, World.. Again!";
        return x;
    });

    CROW_ROUTE(app, "/json-initializer-list-constructor")
    ([] {
        return crow::json::wvalue({
          {"first", "Hello world!"},                     /* stores a char const* hence a json::type::String */
          {"second", std::string("How are you today?")}, /* stores a std::string hence a json::type::String. */
          {"third", std::int64_t(54)},                   /* stores a 64-bit int hence a std::int64_t. */
          {"fourth", std::uint64_t(54)},                 /* stores a 64-bit unsigned int hence a std::uint64_t. */
          {"fifth", 54},                                 /* stores an int (as 54 is an int literal) hence a std::int64_t. */
          {"sixth", 54u},                                /* stores an unsigned int (as 54u is a unsigned int literal) hence a std::uint64_t. */
          {"seventh", 2.f},                              /* stores a float (as 2.f is a float literal) hence a double. */
          {"eighth", 2.},                                /* stores a double (as 2. is a double literal) hence a double. */
          {"ninth", nullptr},                            /* stores a std::nullptr hence json::type::Null . */
          {"tenth", true}                                /* stores a bool hence json::type::True . */
        });
    });

    // json list response
    CROW_ROUTE(app, "/json_list")
    ([] {
        crow::json::wvalue x(crow::json::wvalue::list({1, 2, 3}));
        return x;
    });

    // To see it in action enter {ip}:18080/hello/{integer_between -2^32 and 100} and you should receive
    // {integer_between -2^31 and 100} bottles of beer!
    CROW_ROUTE(app, "/hello/<int>")
    ([](int count) {
        if (count > 100)
            return crow::response(400);
        std::ostringstream os;
        os << count << " bottles of beer!";
        return crow::response(os.str());
    });

    // Same as above, but using crow::status
    CROW_ROUTE(app, "/hello_status/<int>")
    ([](int count) {
        if (count > 100)
            return crow::response(crow::status::BAD_REQUEST);
        std::ostringstream os;
        os << count << " bottles of beer!";
        return crow::response(os.str());
    });

    // To see it in action submit {ip}:18080/add/1/2 and you should receive 3 (exciting, isn't it)
    CROW_ROUTE(app, "/add/<int>/<int>")
    ([](crow::response& res, int a, int b) {
        std::ostringstream os;
        os << a + b;
        res.write(os.str());
        res.end();
    });

    //same as the example above but uses mustache instead of stringstream
    CROW_ROUTE(app, "/add_mustache/<int>/<int>")
    ([](int a, int b) {
        auto t = crow::mustache::compile("Value is {{func}}");
        crow::mustache::context ctx;
        ctx["func"] = [&](std::string) {
            return std::to_string(a + b);
        };
        auto result = t.render(ctx);
        return result;
    });

    // Compile error with message "Handler type is mismatched with URL paramters"
    //CROW_ROUTE(app,"/another/<int>")
    //([](int a, int b){
    //return crow::response(500);
    //});

    // more json example

    // To see it in action, I recommend to use the Postman Chrome extension:
    //      * Set the address to {ip}:18080/add_json
    //      * Set the method to post
    //      * Select 'raw' and then JSON
    //      * Add {"a": 1, "b": 1}
    //      * Send and you should receive 2

    // A simpler way for json example:
    //      * curl -d '{"a":1,"b":2}' {ip}:18080/add_json
    CROW_ROUTE(app, "/add_json")
      .methods("POST"_method)([](const crow::request& req) {
          auto x = crow::json::load(req.body);
          if (!x)
              return crow::response(400);
          int sum = x["a"].i() + x["b"].i();
          std::ostringstream os;
          os << sum;
          return crow::response{os.str()};
      });

    // Example of a request taking URL parameters
    // If you want to activate all the functions just query
    // {ip}:18080/params?foo='blabla'&pew=32&count[]=a&count[]=b
    CROW_ROUTE(app, "/params")
    ([](const crow::request& req) {
        std::ostringstream os;

        // To get a simple string from the url params
        // To see it in action /params?foo='blabla'
        os << "Params: " << req.url_params << "\n\n";
        os << "The key 'foo' was " << (req.url_params.get("foo") == nullptr ? "not " : "") << "found.\n";

        // To get a double from the request
        // To see in action submit something like '/params?pew=42'
        if (req.url_params.get("pew") != nullptr)
        {
            double countD = crow::utility::lexical_cast<double>(req.url_params.get("pew"));
            os << "The value of 'pew' is " << countD << '\n';
        }

        // To get a list from the request
        // You have to submit something like '/params?count[]=a&count[]=b' to have a list with two values (a and b)
        auto count = req.url_params.get_list("count");
        os << "The key 'count' contains " << count.size() << " value(s).\n";
        for (const auto& countVal : count)
        {
            os << " - " << countVal << '\n';
        }

        // To get a dictionary from the request
        // You have to submit something like '/params?mydict[a]=b&mydict[abcd]=42' to have a list of pairs ((a, b) and (abcd, 42))
        auto mydict = req.url_params.get_dict("mydict");
        os << "The key 'dict' contains " << mydict.size() << " value(s).\n";
        for (const auto& mydictVal : mydict)
        {
            os << " - " << mydictVal.first << " -> " << mydictVal.second << '\n';
        }

        return crow::response{os.str()};
    });

    CROW_ROUTE(app, "/large")
    ([] {
        return std::string(512 * 1024, ' ');
    });

    // Take a multipart/form-data request and print out its body
    CROW_ROUTE(app, "/multipart")
    ([](const crow::request& req) {
        crow::multipart::message_view msg(req);
        CROW_LOG_INFO << "body of the first part " << msg.parts[0].body;
        return "it works!";
    });

    // enables all log
    app.loglevel(crow::LogLevel::Debug);
    //crow::logger::setHandler(std::make_shared<ExampleLogHandler>());

    app.port(18080)
      .multithreaded()
      .run();
}
