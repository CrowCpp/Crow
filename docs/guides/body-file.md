<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

By default Crow stores a request body in `req.body` (`std::string`). That is
the right default for JSON and form fields, but a large upload then occupies
the same amount of RAM as the file.

A route can divert body bytes **while they arrive** to a sink instead, via
`#!cpp .body_sink(factory)`. The built-in sink is a unique file
(`crow::FileBodySink`, an opt-in header — see below); you can also supply
your own (flash, SD, a bounded buffer) when there is no writable filesystem.

The handler still runs only after the full body has been received. Writes run
synchronously on the connection's io thread, before the `Host` header check,
before middleware, and before `100 Continue`. A sink that throws, or returns
`false` from `write()`/`finish()`, ends the request as a 500 with the
connection closed; the route handler never runs.

This is the request-side counterpart of returning a file with
`response.set_static_file_info()`: the payload is never held as one string.

Size limits are `app.max_body_size()` / `.max_body_size()` — see
[Request body size](app.md#request-body-size). Over-limit requests are 413
and the connection closes; nothing the sink wrote is kept.

## File convenience

```cpp
#include "crow.h"
#include "crow/file_body_sink.h" // opt-in: not pulled in by crow.h

crow::SimpleApp app;
app.max_body_size(64ull * 1024 * 1024);

CROW_ROUTE(app, "/upload")
  .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Post)
  .body_sink(crow::FileBodySink::factory("uploads")) // empty: system temp directory
  ([](const crow::request& req) {
      auto* file = crow::FileBodySink::from(req);
      if (!file)
          return crow::response(200); // e.g. an empty PUT: see "Empty bodies" below
      // req.body is empty; the bytes are in file->path()
      std::ifstream in(file->path(), std::ios::binary);
      // ...
      return crow::response(200);
  });
```

`crow::FileBodySink::factory(directory)` resolves `directory` to an absolute
path once, at the point you call it, and throws
`std::filesystem::filesystem_error` if it does not already exist — Crow does
not create it for you, and does not fail silently. Crow always generates a
unique file name (POSIX: `mkostemp`; Windows: a retried `CREATE_NEW`), so
concurrent requests never share a path. The descriptor is kept open until the
body is complete; the handler then reads `file->path()`.

The file is deleted when the last `crow::request` copy that reached the
handler is destroyed — normally right after the response has been fully
sent. Call `file->keep()` if the application will use the file after that
(for example after renaming it into place); the destructor then only closes
the descriptor.

A client that disconnects before the body is complete never reaches the
handler; the partial file is removed. Bodyless GET/HEAD (and
`Content-Length: 0`) do not create a file — `crow::FileBodySink::from(req)`
returns `nullptr` in that case, same as for any route where the body didn't
go to this sink.

`crow::FileBodySink::from()` uses `dynamic_cast` and so needs RTTI; that is
why this header is opt-in and never pulled in by `crow.h` or
`crow/parser.h`. A build with `-fno-rtti` (or asio's `-DASIO_NO_TYPEID`) can
use `crow::BodySink` directly and simply not include
`crow/file_body_sink.h`.

## Custom sink

```cpp
struct FlashSink : crow::BodySink {
    bool write(const char* data, std::size_t length) override;
    bool finish() override;
};

CROW_ROUTE(app, "/upload")
  .methods(crow::HTTPMethod::Post)
  .body_sink([](const crow::request& req) -> std::unique_ptr<crow::BodySink> {
      return std::make_unique<FlashSink>(req);
  })
  ([](const crow::request&) {
      return crow::response(200);
  });
```

The factory runs once per request, after headers and before the body; `req`
carries the method, url, headers and `remote_ip_address` (already set, so a
factory can apply a per-peer policy), but not `body` or `body_sink` yet.
Returning `nullptr` from the factory means "no sink for this request" — the
body goes to `req.body` as usual, it is not an error. Throwing from the
factory, or from `write()`/`finish()`, is a 500.

Calling `.body_sink(...)` again on the same route replaces the factory
(last-call-wins).

## Empty bodies

A `.body_sink(...)` route only opens a sink when a body is actually coming
(`Content-Length` > 0, or chunked framing); a bodyless request never calls
the factory, so `req.body_sink` (and `crow::FileBodySink::from(req)`) is
`nullptr`. Handle that case explicitly — see the example above. Note that an
empty **chunked** body (`0\r\n\r\n`) still counts as "a body is coming" and
does open the sink (into an empty file, for the built-in one), unlike
`Content-Length: 0`.

## What this is not

`.body_sink(...)` stores the **raw** request body. `multipart/form-data` is
parsed from `req.body` by `crow::multipart::message`; a route with a sink
diverts every body regardless of `Content-Type`, so `req.body` is empty and
`crow::multipart::message` cannot be used on it. Saving individual multipart
parts as they arrive is a separate feature.

The route handler is not invoked per chunk. Incremental processing belongs in
`BodySink::write`.
