#define CROW_MAIN
#define CROW_JSON_USE_MAP
#include "crow.h"

int main()
{
    crow::SimpleApp app;

// simple json response using a map
// To see it in action enter {ip}:18080/json
// it shoud show amessage before zmessage despite adding zmessage first.
CROW_ROUTE(app, "/json")
([]{
    crow::json::wvalue x;
    x["zmessage"] = "Hello, World!";
    x["amessage"] = "Hello, World2!";
    return x;
});

CROW_ROUTE(app, "/json-initializer-list-constructor")
([] {
  return crow::json::wvalue({
    {"first", "Hello world!"},                     /* stores a char const* hence a json::type::String */
    {"second", std::string("How are you today?")}, /* stores a std::string hence a json::type::String. */
    {"third", 54},      /* stores an int (as 54 is an int literal) hence a std::int64_t. */
    {"fourth", 54l},    /* stores a long (as 54l is a long literal) hence a std::int64_t. */
    {"fifth", 54u},     /* stores an unsigned int (as 54u is a unsigned int literal) hence a std::uint64_t. */
    {"sixth", 54ul},    /* stores an unsigned long (as 54ul is an unsigned long literal) hence a std::uint64_t. */
    {"seventh", 2.f},   /* stores a float (as 2.f is a float literal) hence a double. */
    {"eighth", 2.},     /* stores a double (as 2. is a double literal) hence a double. */
    {"ninth", nullptr}, /* stores a std::nullptr hence json::type::Null . */
    {"tenth", true}     /* stores a bool hence json::type::True . */
  });
});

// enables all log
app.loglevel(crow::LogLevel::Debug);

app.port(18080)
    .multithreaded()
    .run();
}
