Crow contains some middlewares that are ready to be used in your application.
<br>
Make sure you understand how to enable and use [middleware](../middleware/).

## Sessions
Include: `crow/middlewares/session.h` <br>
Examples: `examples/middlewares/session.cpp`

This middleware can be used for managing sessions - small packets of data associated with a single client that persist across multiple requests. Sessions shouldn't store anything permanent, but only context that is required to easily work with the current client (is the user authenticated, what page did he visit last, etc.).

### Setup

Session data can be stored in multiple ways:

* `crow::InMemoryStore` - stores all data in memory
* `crow::FileStore` - stores all all data in json files
* A custom store

__Always list the CookieParser before the Session__
```cpp
using Session = crow::SessionMiddleware<crow::FileStore>;
crow::App<crow::CookieParser, Session> app{Session{
  crow::FileStore{"/tmp/sessiondata"}
}};
```

Session ids are represented as random alphanumeric strings and are stored in cookies. See the examples for more customization options.

### Usage

A session is basically a key-value map with support for multiple types: strings, integers, booleans and doubles. The map is created and persisted automatically as soon it is first written to.

```cpp
auto& session = app.get_context<Session>(request);

session.get("key", "not-found"); // get string by key and return "not-found" if not found
session.get("int", -1);
session.get<bool>("flag"); // returns default value(false) if not found

session.set("key", "new value");
session.string("any-type"); // return any type as string representation
session.remove("key");
session.keys(); // return list of keys
```

Session objects are shared between concurrent requests,
this means we can perform atomic operations and even lock the object.
```cpp
session.apply("views", [](int v){return v + 1;}); // this operation is always atomic, no way to get a data race
session.mutex().lock(); // manually lock session
```

### Expiration

Expiration can happen either by the cookie expiring or the store deleting "old" data.

* By default, cookies expire after 30 days. This can be changed with the cookie option in the Session constructor. 
* `crow::FileStore` automatically supports deleting files that are expired (older than 30 days). The expiration age can also be changed in the constructor.

The session expiration can be postponed. This will make the Session issue a new cookie and make the store acknowledge the new expiration time.
```cpp
session.refresh_expiration()
```

## Cookies
Include: `crow/middlewares/cookie_parser.h` <br>
Examples: `examples/middlewares/example_cookies.cpp`

This middleware allows to read and write cookies by using `CookieParser`. Once enabled, it parses all incoming cookies.

Cookies can be read and written with the middleware context. All cookie attributes can be changed as well.

```cpp
auto& ctx = app.get_context<crow::CookieParser>(request);
std::string value = ctx.get_cookie("key");
ctx.set_cookie("key", "value")
    .path("/")
    .max_age(120);
```

!!! note

    Make sure `CookieParser` is listed before any other middleware that relies on it.

## CORS
Include: `crow/middlewares/cors.h` <br>
Examples: `examples/middlewares/example_cors.cpp`

This middleware allows to set CORS policies by using `CORSHandler`. Once enabled, it will apply the default CORS rules globally.

The CORS rules can be modified by first getting the middleware via `#!cpp auto& cors = app.get_middleware<crow::CORSHandler>();`. The rules can be set per URL prefix using `prefix()`, per blueprint using `blueprint()`, or globally via `global()`. These will return a `CORSRules` object which contains the actual rules for the prefix, blueprint, or application. For more details go [here](../reference/structcrow_1_1_c_o_r_s_handler.html).

`CORSRules` can  be modified using the methods `origin()`, `methods()`, `headers()`, `max_age()`, `allow_credentials()`, or `ignore()`. For more details on these methods and what default values they take go [here](../reference/structcrow_1_1_c_o_r_s_rules.html).

```cpp
auto& cors = app.get_middleware<crow::CORSHandler>();
cors
  .global()
    .headers("X-Custom-Header", "Upgrade-Insecure-Requests")
    .methods("POST"_method, "GET"_method)
  .prefix("/cors")
    .origin("example.com");
```
