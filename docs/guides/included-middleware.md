Crow contains some middlewares that are ready to be used in your application.
<br>
Make sure you understand how to enable and use [middleware](../middleware/).

## Cookies
Include: `crow/middlewares/cookie_parser.h` <br>
Examples: `examples/middlewars/example_cookies.cpp`

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
Examples: `examples/middlewars/example_cors.cpp`

This middleware allows to set CORS policies by using `CORSHandler`. Once enabled, it will apply the default CORS rules globally.

The CORS rules can be modified by first getting the middleware via `#!cpp auto& cors = app.get_middleware<crow::CORSHandler>();`. The rules can be set per URL prefix using `prefix()`, per blueprint using `blueprint()`, or globally via `global()`. These will return a `CORSRules` object which contains the actual rules for the prefix, blueprint, or application. For more details go [here](../../reference/structcrow_1_1_c_o_r_s_handler.html).

`CORSRules` can  be modified using the methods `origin()`, `methods()`, `headers()`, `max_age()`, `allow_credentials()`, or `ignore()`. For more details on these methods and what default values they take go [here](../../reference/structcrow_1_1_c_o_r_s_rules.html).

```cpp
auto& cors = app.get_middleware<crow::CORSHandler>();
cors
  .global()
    .headers("X-Custom-Header", "Upgrade-Insecure-Requests")
    .methods("POST"_method, "GET"_method)
  .prefix("/cors")
    .origin("example.com");
```