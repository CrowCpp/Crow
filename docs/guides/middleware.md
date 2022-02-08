Middleware is used for altering and inspecting requests before and after the handler call.

Any middleware requires the following 3 members:

* A context struct for storing the middleware data.
* A `before_handle` method, which is called before the handler. If `res.end()` is called, the operation is halted.
* A `after_handle` method, which is called after the handler.

## before_handle
There are two possible signatures for before_handle

1. if you only need to access this middleware's context.

```cpp
void before_handle(request& req, response& res, context& ctx)
```

2. To get access to other middlewares context
``` cpp
template <typename AllContext>
void before_handle(request& req, response& res, context& ctx, AllContext& all_ctx) 
{
    auto other_ctx = all_ctx.template get<OtherMiddleware>();
}
```


## after_handle
There are two possible signatures for after_handle

1. if you only need to access this middleware's context.

```cpp
void after_handle(request& req, response& res, context& ctx)
```

2. To get access to other middlewares context
``` cpp
template <typename AllContext>
void after_handle(request& req, response& res, context& ctx, AllContext& all_ctx) 
{
    auto other_ctx = all_ctx.template get<OtherMiddleware>();
}
```

## Using middleware

All middleware has to be registered in the Crow application and is enabled globally by default.

```cpp
crow::App<FirstMiddleware, SecondMiddleware> app;
```

if you want to enable some middleware only for specific handlers, you have to extend it from `crow::ILocalMiddleware`.

```cpp
struct LocalMiddleware : crow::ILocalMiddleware 
{
... 
```

After this, you can enable it for specific handlers.

```cpp
CROW_ROUTE(app, "/with_middleware")
.CROW_MIDDLEWARES(app, LocalMiddleware)
([]() {
    return "Hello world!";
});
```

## Examples

A local middleware that can be used to guard admin handlers

```cpp
struct AdminAreaGuard : crow::ILocalMiddleware
{
    struct context
    {};

    void before_handle(crow::request& req, crow::response& res, context& ctx)
    {
        if (req.remote_ip_address != ADMIN_IP) 
        {
            res.code = 403;
            res.end();
        }
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx)
    {} 
};
```