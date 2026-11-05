Middleware is used for altering and inspecting requests before and after calling the handler. In Crow it's very similar to middleware in other web frameworks.

All middleware is registered in the Crow application

```cpp
crow::App<FirstMW, SecondMW, ThirdMW> app;
```

and is called in this specified order.

Any middleware requires the following 3 members:

* A context struct for storing request local data.
* A `before_handle` method, which is called before the handler.
* A `after_handle` method, which is called after the handler.

!!! warning

    As soon as `response.end()` is called, no other handlers and middleware is run, except for after_handlers of already visited middleware.


## Example

A middleware that can be used to guard admin handlers

```cpp
struct AdminAreaGuard
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


### before_handle and after_handle
There are two possible signatures for before_handle and after_handle

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

## Local middleware

By default, every middleware is called for each request. If you want to enable middleware for specific handlers or blueprints, you have to extend it from `crow::ILocalMiddleware`

```cpp
struct LocalMiddleware : crow::ILocalMiddleware
{
```

After this, you can enable it for specific handlers

```cpp
CROW_ROUTE(app, "/with_middleware")
.CROW_MIDDLEWARES(app, LocalMiddleware)
([]() {
    return "Hello world!";
});
```

or blueprints

```cpp
Blueprint bp("with_middleware");
bp.CROW_MIDDLEWARES(app, FistLocalMiddleware, SecondLocalMiddleware);
```

!!! warning

    Local and global middleware are called separately. First all global middleware is run, then all enabled local middleware for the current handler is run. In both cases middleware is called strongly
    in the order listed in the Crow application.
