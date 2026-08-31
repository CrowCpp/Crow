<span class="tag">[:octicons-feed-tag-16: master](https://github.com/CrowCpp/Crow)</span>

By default Crow stores a request body in `req.body` (`std::string`). That is
the right default for JSON and form fields, but a large upload then occupies
the same amount of RAM as the file.

A route can ask Crow to write the body to a unique file **while the bytes
arrive**. `req.body` stays empty; the handler uses `req.body_file_path`.

This is the request-side counterpart of returning a file with
`response.set_static_file_info()`: the payload is never held as one string.

## Route option

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
do not share a path.

## App options

```cpp
app.body_file_directory("uploads");          // default: system temp directory
app.max_body_file_size(64ull * 1024 * 1024); // default: 0 = unlimited
```

When the written size would exceed `max_body_file_size()`, Crow discards the
partial file, still consumes the rest of the request so keep-alive stays in
sync, and responds `413 Payload Too Large` without running the handler.

## Lifetime

The file is deleted after the response is sent. Call `req.keep_body_file()`
if the application will use the file after the handler returns (for example
after renaming it into place). Copy `body_file_path` if another thread will
open it.

A client that disconnects before the body is complete never reaches the
handler; the partial file is removed.

## What this is not

`.body_file()` stores the **raw** request body. `multipart/form-data` is still
parsed from `req.body` by `crow::multipart::message`. Saving individual
multipart parts to disk as they arrive is a separate feature.

The handler still runs only after the full body has been received. The gain
is bounded memory, not an incremental handler API.
