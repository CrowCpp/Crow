A response body whose size is not known in advance, or which is simply too large
to fit in memory, can be produced on demand and sent using
[chunked transfer encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding).

Call `#!cpp response.set_chunked_content_provider(<provider>, <mime-type>)` with a
callable that produces the body one piece at a time. Crow sets
`Transfer-Encoding: chunked`, omits `Content-Length`, and calls the provider
repeatedly while writing the response.

## The provider

```cpp
bool provider(std::string& chunk);
```

Fill `chunk` with the next piece of the body and return `#!cpp true` while more
data is coming, `#!cpp false` on the last invocation. Leaving `chunk` empty is
allowed as an occasional occurrence and sends nothing. A provider that has no
data yet should block until data is available (or finish the transfer): the
provider is called again immediately, so returning `#!cpp true` with an empty
chunk in a tight loop spins the connection thread needlessly.

### Example

```cpp
auto app = crow::SimpleApp();

CROW_ROUTE(app, "/numbers")
([](const crow::request&, crow::response& res) {
    int remaining = 100;
    res.set_chunked_content_provider(
      [remaining](std::string& chunk) mutable -> bool {
          if (remaining == 0)
              return false;
          chunk = std::to_string(100 - remaining) + '\n';
          --remaining;
          return true;
      },
      "text/plain");
    res.end();
});
```

## Aborting the transfer

A provider that discovers midway that the body cannot be finished (the source of
the data failed, for example) should not let the response end normally: without
`Content-Length`, the terminating frame is the only thing that tells the client
the body is complete. For this case the provider can return
`#!cpp crow::chunk_result` instead of `#!cpp bool`:

```cpp
crow::chunk_result provider(std::string& chunk);
```

Return `#!cpp crow::chunk_result::more` while more data is coming,
`#!cpp crow::chunk_result::done` on the last invocation, or
`#!cpp crow::chunk_result::abort` to stop the transfer. On `abort` Crow closes
the connection without sending the terminating frame, so the client sees a
truncated body instead of a seemingly complete one.

```cpp
CROW_ROUTE(app, "/file")
([](const crow::request&, crow::response& res) {
    auto file = open_source_somehow();
    res.set_chunked_content_provider(
      [file](std::string& chunk) mutable -> crow::chunk_result {
          if (!file->read(chunk))
              return crow::chunk_result::abort; // reading failed: truncate the body
          return chunk.empty() ? crow::chunk_result::done : crow::chunk_result::more;
      },
      "application/octet-stream");
    res.end();
});
```

## Asynchronous provider

Use `#!cpp set_async_chunked_content_provider` when the data source becomes
ready asynchronously. The provider receives a completion callback for one
requested chunk:

```cpp
void provider(crow::response::async_chunk_completion_t complete);
```

The provider must return promptly and call `complete` exactly once for that
invocation. It may call `complete` inline before returning, or later from any
thread. Pass `#!cpp crow::chunk_result::more` when more data remains,
`#!cpp crow::chunk_result::done` for the final chunk, or
`#!cpp crow::chunk_result::abort` when the body cannot be completed. A nonempty
chunk passed with `done` is written before the terminating frame. `more` with
an empty chunk requests another chunk without writing a frame; `done` with an
empty chunk writes only the terminating frame. `abort` discards its chunk.

```cpp
auto source = open_async_source_somehow();

CROW_ROUTE(app, "/events")
([source](const crow::request&, crow::response& res) {
    res.set_async_chunked_content_provider(
      [source](crow::response::async_chunk_completion_t complete) {
          source->read_next(
            [complete = std::move(complete)](std::error_code error,
                                             std::string data,
                                             bool finished) mutable {
                if (error) {
                    complete(crow::chunk_result::abort, "");
                    return;
                }
                complete(finished ? crow::chunk_result::done
                                  : crow::chunk_result::more,
                         std::move(data));
            });
      },
      "text/event-stream");
    res.set_chunked_completion_handler([](bool clean) {
        if (!clean)
            CROW_LOG_WARNING << "asynchronous transfer did not finish cleanly";
    });
    res.end();
});
```

Crow posts every completion result to the connection executor, including a
result delivered inline. At most one provider request and one socket write are
pending, and Crow requests the next chunk only after the preceding write has
finished. This bounds the transfer to one current payload chunk and provides
backpressure without an unbounded queue.

Calling `set_async_chunked_content_provider` discards any string body, static
file, or synchronous provider already configured on the response. Calling a
synchronous provider setter or a static-file setter afterward releases the
asynchronous provider. The response therefore has one body source.

Installing an empty `async_chunk_provider_t` still selects asynchronous
streaming. When Crow reaches the provider invocation, it treats the missing
callable as a provider failure: the connection closes without a terminating
frame, and the completion handler runs exactly once with `clean == false`.

## Completion handler

To find out how the transfer ended (to release the source of the data, or to log
a failure), set a handler that is called once after the body has been written:

```cpp
res.set_chunked_completion_handler([](bool clean) {
    if (!clean)
        CROW_LOG_WARNING << "chunked transfer did not finish cleanly";
});
```

The handler is called exactly once. `clean` is `#!cpp true` when the provider
finished normally (`#!cpp crow::chunk_result::done`, or `#!cpp false` from the
`bool` provider) and every write succeeded; it is `#!cpp false` when the
provider aborted or a write error occurred. An exception raised before provider
completion is treated as an abort. An asynchronous transfer that is still
active during server shutdown completes with `clean == false`.

During normal completion, the handler runs on the connection's thread after the
last body write and before the response is finalized. Server worker shutdown
also invokes it on that connection worker before the worker exits. If a
`Connection` is owned directly outside the server lifecycle, destruction is the
fallback cleanup path and invokes the handler on the destruction thread. For a
`HEAD` request the provider is never called, but the handler still runs with
`clean == true` when the response ends, so it is a reliable place to release
the data source. The handler should not throw: an exception that escapes it is
logged and swallowed.

## Notes

!!! note

    The provider runs on the connection's thread while the response is being
    written, so a provider that blocks keeps that thread busy for the whole
    transfer. This note applies to `set_chunked_content_provider`. An
    asynchronous provider must return promptly and signal readiness through its
    completion callback.

!!! note

    The connection deadline is cancelled for the duration of the transfer.
    Without that, a body that takes longer to produce than the timeout would be
    cut short by the connection being closed.

!!! note

    A response to a `HEAD` request releases the provider without calling it: the
    headers are sent and the body is skipped. The completion handler still runs,
    with `clean == true`. Chunked framing remains represented in the headers,
    with `Transfer-Encoding: chunked` retained and `Content-Length` omitted.

!!! note

    A write error in the middle of the transfer is treated like `abort` as far
    as the connection is concerned: the terminating frame is not sent and the
    connection is closed instead of being reused for keep-alive.

!!! note

    Chunked providers require HTTP/1.1. When a handler configures either a
    synchronous or asynchronous chunk provider for an HTTP/1.0 request, Crow
    does not invoke the provider. It replaces the streaming response with a
    finite `505 HTTP Version Not Supported` response, removes
    `Transfer-Encoding`, adds `Content-Length`, closes the connection, and
    invokes the completion handler exactly once with `clean == false`.

!!! note

    Clean completion writes the terminating frame before the response is
    finalized. If the request permits keep-alive, Crow clears the completed
    response and parser state, restores the connection deadline, and reads the
    next request from the same connection. `abort`, provider exceptions, and
    write errors close the connection without a terminating frame. A late or
    repeated asynchronous completion result is ignored.
