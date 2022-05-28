#include "crow.h"
#include "crow/middlewares/session.h"

crow::response redirect() {
    crow::response rsp;
    rsp.redirect("/");
    return rsp;
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
    crow::App<crow::CookieParser, Session> app {Session{
        // choose a secret key for sigining cookies
        "MY_SECRET_KEY", 
        // customize cookies
        crow::CookieParser::Cookie("session").max_age(/*one day*/24 * 60 * 60).path("/"),
        // set session_id length (small value only for demonstration purposes)
        4, 
        // init the store
        crow::InMemoryStore{}
    }};

    // List all values
    CROW_ROUTE(app, "/")
    ([&](const crow::request& req) {
        // get session as middleware context
        auto& session = app.get_context<Session>(req);

        // atomically increase number of views
        // if "views" doesn't exist, it'll be default initialized
        session.apply("views", [](int v){ return v + 1; });

        // get all currently present keys
        auto keys = session.keys();

        std::string out;
        for (const auto& key: keys) out += "<p> " + key + " = " + session.string(key) + "</p>";
        return out;
    });

    // Get a key
    CROW_ROUTE(app, "/get")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");

        // cast value to string
        auto string_v = session.get<std::string>(key, "_NOT_FOUND_");
        (void)string_v;

        // cast value to int
        int int_v = session.get<int>(key, -1);
        (void)int_v;

        // get string representation
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

    // Evict a key
    CROW_ROUTE(app, "/evict")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);
        auto key = req.url_params.get("key");
        session.evict(key);

        return redirect();
    });

    // Manually lock a session for synchronization in parallel requests
    CROW_ROUTE(app, "/lock")
    ([&](const crow::request& req) {
        auto& session = app.get_context<Session>(req);

        std::lock_guard<std::recursive_mutex> l(session.mutex());

        if (session.get("views", 0) % 2 == 0) {
            session.set("even", true);
        } else {
            session.evict("even");
        }

        return redirect();
    });

    app.port(18080)
        //.multithreaded()
        .run();

    return 0;
}
