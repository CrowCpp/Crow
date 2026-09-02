A Crow app defines an interface to allow the developer access to all the different parts of the framework, without having to manually deal with each one.<br><br>
An app allows access to the HTTP server (for handling connections), router (for handling URLs and requests), Middlewares (for extending Crow), among many others.<br><br>

Crow has 2 different app types:

## SimpleApp
Has no middlewares.

## App&lt;m1, m2, ...&gt;
Has middlewares.

## Using the app
To use a Crow app, simply define `#!cpp crow::SimpleApp` or `#!cpp crow::App<m1, m2 ...>` if you're using middlewares.<br>
The methods of an app can be chained. That means that you can configure and run your app in the same code line.
``` cpp
app.bindaddr("192.168.1.2").port(443).ssl_file("certfile.crt","keyfile.key").multithreaded().run();
```
Or if you like your code neat
``` cpp
app.bindaddr("192.168.1.2")
.port(443)
.ssl_file("certfile.crt","keyfile.key")
.multithreaded()
.run();
```

!!! note

    The `run()` method is blocking. To run a Crow app asynchronously `run_async()` should be used instead.

!!! warning

    When using `run_async()`, make sure to use a variable to save the function's output (such as `#!cpp auto _a = app.run_async()`). Otherwise the app will run synchronously.

## TCP socket options
<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

Crow lets you control TCP_NODELAY for accepted connections.

- `app.tcp_nodelay(true)` enables TCP_NODELAY on HTTP server connections.
- `app.websocket_tcp_nodelay(true)` enables TCP_NODELAY on WebSocket connections.

These settings can be configured independently.

```cpp
crow::SimpleApp app;

app.tcp_nodelay(true)
   .websocket_tcp_nodelay(true)
   .port(18080)
   .multithreaded()
   .run();
```

## Request body size
<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

By default Crow accepts a request body of any size. `#!cpp app.max_body_size(bytes)` sets an app-wide limit (`UINT64_MAX` is unlimited). A route can override it with `#!cpp .max_body_size(bytes)`.

The advertised `Content-Length` is checked when headers complete, before `100 Continue`. Chunked bodies are counted as they arrive. An over-limit request is answered `413 Payload Too Large` and the connection is closed; the body is not stored, leftover bytes are drained and discarded, and the route handler does not run. The cap applies to every request that reaches this check, including matched routes, 404, 405, and slash-redirects — but not an unmatched `HEAD` or `OPTIONS` request, which is answered before headers complete and reads no body either way. Only global after-handlers run on the 413; route-local middleware and before-handlers do not.

```cpp
crow::SimpleApp app;
app.max_body_size(64ull * 1024 * 1024);

CROW_ROUTE(app, "/upload")
  .methods(crow::HTTPMethod::Post)
  .max_body_size(8ull * 1024 * 1024)
  ([](const crow::request& req) {
      return crow::response(200);
  });
```

<br><br>

For more info on middlewares, check out [this page](middleware.md).<br><br>
For more info on what functions are available to a Crow app, go [here](../reference/classcrow_1_1_crow.html).
