#include "crow.h"
#include "crow/middlewares/session.h"

crow::response redirect()
{
    crow::response res;
    res.redirect("/");
    return res;
}

int main()
{
    // Choose a storage kind for:
    // - InMemoryStore stores all entries in memory
    // - FileStore stores all entries in json files
    using Session = crow::SessionMiddleware<crow::InMemoryStore>;

    // Writing your own store is easy
    // Check out the existing ones for guidelines

    // Make sure the CookieParser is registered before the Session
    crow::App<crow::CookieParser, Session> app{Session{
      // customize cookies
      crow::CookieParser::Cookie("session").max_age(/*one day*/ 24 * 60 * 60).path("/"),
      // set session id length (small value only for demonstration purposes)
      4,
      // init the store
      crow::InMemoryStore{}}};

    // List all values
    CROW_ROUTE(app, "/")
    ([&](const crow::request& req) {
        // get session as middleware context
        auto& session = app.get_context<Session>(req);
        // the session acts as a multi-type map
        // that can store string, integers, doubles and bools
        // besides get/set/remove it also supports more advanced locking operations

        // Atomically increase number of views
        // This will not skip a view even on multithreaded applications
        // with multiple concurrent requests from a client
        // if "views" doesn't exist, it'll be default initialized
        session.apply("views", [](int v) {
            return v + 1;
        });

        // get all currently present keys
        auto keys = session.keys();

        std::string out;
        for (const auto& key : keys)
            // .string(key) converts a value of any type to a string
            out += "<p> " + key + " = " + session.string(key) + "</p>";
        return out;
    });

    // Get a key
    CROW_ROUTE(app, "/get")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");

        // get a string
        // return "_NOT_FOUND_" if value is not found or of another type
        std::string string_v = session.get(key, "_NOT_FOUND_");
        // alternatively one can use
        // session.get<std::string>(key)
        // where the fallback is an empty value ""
        (void)string_v;

        // get int
        // because supporting multiple integer types in a type bound map would be cumbersome,
        // all integral values (except uint64_t) are promoted to int64_t
        // that is why get<int>, get<uint32_t>, get<int64_t> are all accessing the same type
        int int_v = session.get(key, -1);
        (void)int_v;

        return session.string(key);
    });

    // Set a key
    // A session is stored as soon as it becomes non empty
    CROW_ROUTE(app, "/set")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);

        auto key = req.url_params.get("key");
        auto value = req.url_params.get("value");

        session.set(key, value);

        return redirect();
    });

    // Remove a key
    CROW_ROUTE(app, "/remove")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");
        session.remove(key);

        return redirect();
    });

    // Manually lock a session for synchronization in parallel requests
    CROW_ROUTE(app, "/lock")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);

        std::lock_guard<std::recursive_mutex> l(session.mutex());

        if (session.get("views", 0) % 2 == 0)
        {
            session.set("even", true);
        }
        else
        {
            session.remove("even");
        }

        return redirect();
    });

    app.port(18080)
      //.multithreaded()
      .run();

    return 0;
}
