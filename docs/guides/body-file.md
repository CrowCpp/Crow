<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

By default Crow stores a request body in `req.body` (`std::string`). That is
the right default for JSON and form fields, but a large upload then occupies
the same amount of RAM as the file.

A route can divert body bytes **while they arrive** to a sink. The built-in
sink is a unique file; you can also supply your own (flash, SD, a bounded
buffer) when there is no writable filesystem.

The handler still runs only after the full body has been received. Writes run
synchronously on the connection's io thread.

This is the request-side counterpart of returning a file with
`response.set_static_file_info()`: the payload is never held as one string.

Size limits are `app.max_body_size()` / `.max_body_size()` — see
[Request body size](app.md#request-body-size). Over-limit requests are 413
and the connection closes; no file is left behind.

## File convenience

```cpp
CROW_ROUTE(app, "/upload")
  .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Post)
  .body_file()([](const crow::request& req) {
      if (!req.has_body_file())
          return crow::response(500);
      // req.body is empty; the bytes are in req.body_file_path
      std::ifstream in(req.body_file_path, std::ios::binary);
      // ...
      return crow::response(200);
  });
```

Pass a directory to `.body_file(<directory>)` to override the app default for
that route. Crow always generates a unique file name, so concurrent requests
do not share a path. The descriptor is kept open until the body is complete;
the handler then reads the path.

```cpp
app.body_file_directory("uploads"); // default: system temp directory
app.max_body_size(64ull * 1024 * 1024);
```

The file is deleted after the response is sent. Call `req.take_body_file()`
if the application will use the file after the handler returns (for example
after renaming it into place).

A client that disconnects before the body is complete never reaches the
handler; the partial file is removed. Bodyless GET/HEAD (and `Content-Length: 0`)
do not create a file.

## Custom sink

```cpp
struct FlashSink : crow::BodySink {
    bool write(const char* data, std::size_t length) override;
    bool finish() override;
};

CROW_ROUTE(app, "/upload")
  .methods(crow::HTTPMethod::Post)
  .body_sink([](const crow::request& req) {
      return std::make_unique<FlashSink>(req);
  })
  ([](const crow::request&) {
      return crow::response(200);
  });
```

The factory runs after headers, before the body. Each request gets its own
sink. `req.body` stays empty.

Do not set both `.body_file()` and `.body_sink()` on the same route; the last
call wins.

## What this is not

`.body_file()` / `.body_sink()` store the **raw** request body.
`multipart/form-data` is still parsed from `req.body` by
`crow::multipart::message`. Saving individual multipart parts as they arrive
is a separate feature.

The route handler is not invoked per chunk. Incremental processing belongs in
`BodySink::write`.
