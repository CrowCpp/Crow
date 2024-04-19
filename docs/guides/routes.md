Routes define what happens when your client connects to a certain URL.<br>

## Macro
`CROW_ROUTE(app, url)`<br>
Can be replaced with `#!cpp app.route<crow::black_magick::get_parameter_tag(url)>(url)` or `#!cpp app.route_dynamic(url)` if you're using VS2013 or want runtime URL evaluation. Although this usage is **NOT** recommended.
## App
Which app class to assign the route to.
## Path (URL)
Which relative path is assigned to the route.<br>
Using `/hello` means the client will need to access `http://example.com/hello` in order to access the route.<br>
A path can have parameters, for example `/hello/<int>` will allow a client to input an int into the url which will be in the handler (something like `http://example.com/hello/42`).<br>
Parameters can be `<int>`, `<uint>`, `<double>`, `<string>`, or `<path>`.<br>
It's worth noting that the parameters also need to be defined in the handler, an example of using parameters would be to add 2 numbers based on input:
```cpp
CROW_ROUTE(app, "/add/<int>/<int>")
([](int a, int b)
{
    return std::to_string(a+b);
});
```
you can see the first `<int>` is defined as `a` and the second as `b`. If you were to run this and call `http://example.com/add/1/2`, the result would be a page with `3`. Exciting!

## Methods
You can change the HTTP methods the route uses from just the default `GET` by using `method()`, your route macro should look like `CROW_ROUTE(app, "/add/<int>/<int>").methods(crow::HTTPMethod::GET, crow::HTTPMethod::PATCH)` or `CROW_ROUTE(app, "/add/<int>/<int>").methods("GET"_method, "PATCH"_method)`.

!!! note

    Crow handles `OPTIONS` method automatically. The `HEAD` method is handled automatically unless defined in a route.
    Adding `OPTIONS` to a route's methods has no effect.

Crow defines the following methods:
```
DELETE
GET
HEAD
POST
PUT

CONNECT
OPTIONS
TRACE

PATCH
PURGE

COPY
LOCK
MKCOL
MOVE
PROPFIND
PROPPATCH
SEARCH
UNLOCK
BIND
REBIND
UNBIND
ACL

REPORT
MKACTIVITY
CHECKOUT
MERGE

SEARCH
NOTIFY
SUBSCRIBE
UNSUBSCRIBE

MKCALENDAR

LINK
UNLINK

SOURCE
```

## Handler
Basically a piece of code that gets executed whenever the client calls the associated route, usually in the form of a [lambda expression](https://en.cppreference.com/w/cpp/language/lambda). It can be as simple as `#!cpp ([](){return "Hello World"})`.<br><br>

### Request
Handlers can also use information from the request by adding it as a parameter `#!cpp ([](const crow::request& req){...})`.<br><br>

You can also access the URL parameters in the handler using `#!cpp req.url_params.get("param_name");`. If the parameter doesn't exist, `nullptr` is returned.<br><br>


!!! note "Note &nbsp;&nbsp;&nbsp;&nbsp; <span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>"

    parameters inside the body can be parsed using `#!cpp req.get_body_params();`. which is useful for requests of type `application/x-www-form-urlencoded`. Its format is similar to `url_params`.


For more information on `crow::request` go [here](../reference/structcrow_1_1request.html).<br><br>

### Response
Crow also provides the ability to define a response in the parameters by using `#!cpp ([](crow::response& res){...})`.<br><br>

Please note that in order to return a response defined as a parameter you'll need to use `res.end();`.<br><br>

Alternatively, you can define the response in the body and return it (`#!cpp ([](){return crow::response()})`).<br>

For more information on `crow::response` go [here](../reference/structcrow_1_1response.html).<br><br>
    
Crow defines the following status codes:
```
100 Continue
101 Switching Protocols

200 OK
201 Created
202 Accepted
203 Non-Authoritative Information
204 No Content
205 Reset Content
206 Partial Content

300 Multiple Choices
301 Moved Permanently
302 Found
303 See Other
304 Not Modified
307 Temporary Redirect
308 Permanent Redirect

400 Bad Request
401 Unauthorized
403 Forbidden
404 Not Found
405 Method Not Allowed
406 Not Acceptable
407 Proxy Authentication Required
409 Conflict
410 Gone
413 Payload Too Large
415 Unsupported Media Type
416 Range Not Satisfiable
417 Expectation Failed
428 Precondition Required
429 Too Many Requests
451 Unavailable For Legal Reasons

500 Internal Server Error
501 Not Implemented
502 Bad Gateway
503 Service Unavailable
504 Gateway Timeout
506 Variant Also Negotiates
```

!!! note

    If your status code is not defined in the list above (e.g. `crow::response(123)`) Crow will return `500 Internal Server Error` instead.


### Return statement
A `crow::response` is very strictly tied to a route. If you can have something in a response constructor, you can return it in a handler.<br><br>
The main return type is `std::string`, although you could also return a `crow::json::wvalue` or `crow::multipart::message` directly.<br><br>
For more information on the specific constructors for a `crow::response` go [here](../reference/structcrow_1_1response.html).

## Returning custom classes
<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


If you have your own class you want to return (without converting it to string and returning that), you can use the `crow::returnable` class.<br>
to use the returnable class, you only need your class to publicly extend `crow::returnable`, add a `dump()` method that returns your class as an `std::string`, and add a constructor that has a `Content-Type` header as a string argument.<br><br>

Your class should look like the following:
```cpp
class a : public crow::returnable
{
    a() : returnable("text/plain"){};

    ...
    ...
    ...

    std::string dump() const override
    {
        return this.as_string();
    }
}
```
<br><br>

## Response codes
<span class="tag">[:octicons-feed-tag-16: v1.0](https://github.com/CrowCpp/Crow/releases/v1.0)</span>


Instead of assigning a response code, you can use the `crow::status` enum, for example you can replace `crow::response(200)` with `crow::response(crow::status::OK)`

## Catchall routes
<span class="tag">[:octicons-feed-tag-16: v0.3](https://github.com/CrowCpp/Crow/releases/v0.3)</span>


By default, any request that Crow can't find a route for will return a simple 404 response. You can change that to return a default route using the `CROW_CATCHALL_ROUTE(app)` macro. Defining it is identical to a normal route, even when it comes to the `const crow::request&` and `crow::response&` parameters being optional.
!!! note

    For versions higher than 0.3 (excluding patches), Catchall routes handle 404 and 405 responses. The default response will contain the code 404 or 405.
