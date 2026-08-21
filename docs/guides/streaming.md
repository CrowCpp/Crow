A response body whose size is not known in advance, or which is simply too large
to fit in memory, can be produced on demand and sent using
[chunked transfer encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding).

Both provider forms below drive the same asynchronous engine: chunks are
written with asynchronous socket writes, so a slow client never blocks a
worker thread. Crow sets `Transfer-Encoding: chunked`, omits `Content-Length`,
and requests one chunk at a time; the next chunk is not requested until the
previous one has been written (socket backpressure).

## Synchronous provider

Use `#!cpp set_chunked_content_provider(<provider>, <mime-type>)` when the next
piece of the body is available on demand:

```cpp
bool provider(std::string& chunk);
```

Fill `chunk` with the next piece and return `#!cpp true` while more data is
coming, `#!cpp false` on the last invocation. The provider runs on the
connection's executor. An empty chunk sends nothing; a provider with no data
at hand should block briefly or finish the transfer rather than poll with
empty chunks.

```cpp
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

A provider that discovers midway that the body cannot be finished should not
let the response end normally: without `Content-Length`, the terminating frame
is the only sign that the body is complete. Return
`#!cpp crow::chunk_result` instead of `#!cpp bool` for this case
(`more` / `done` / `abort`): on `abort` Crow closes the connection without the
terminating frame, so the client sees a truncated body.

## Asynchronous provider

Use `#!cpp set_async_chunked_content_provider` when producing a chunk may take
arbitrary time (server-sent events, MJPEG, a message queue). The provider
receives a completion callback for one requested chunk:

```cpp
void provider(crow::response::async_chunk_completion_t complete);
```

The provider must return promptly and call `complete` exactly once per
invocation, inline or later from any thread; Crow publishes the result onto
the connection's executor. Pass `more` while data remains, `done` for the
final chunk (a nonempty chunk is written before the terminating frame), or
`abort` to truncate. `complete` returns `#!cpp false` for a repeated or
inactive result and when publication fails; a source can use that to stop
itself after the client is gone:

```cpp
if (!complete(crow::chunk_result::more, std::move(data))) {
    source->cancel();
    return;
}
```

A complete route: an event stream that forwards messages from a queue and
truncates the body when the source fails.

```cpp
auto queue = std::make_shared<EventQueue>();

CROW_ROUTE(app, "/events")
([queue](const crow::request&, crow::response& res) {
    res.set_async_chunked_content_provider(
      [queue](crow::response::async_chunk_completion_t complete) {
          queue->async_pop([complete = std::move(complete)](Event event) {
              if (event.failed)
              {
                  complete(crow::chunk_result::abort, "");
                  return;
              }
              complete(event.last ? crow::chunk_result::done : crow::chunk_result::more,
                       std::move(event.payload));
          });
      },
      "text/event-stream");
    res.set_chunked_completion_handler([queue](bool) { queue->close(); });
    res.end();
});
```

## Completion handler

`#!cpp set_chunked_completion_handler(void (bool clean))` runs exactly once
after writing stops, on every path: normal completion, abort, provider
exception, write error or timeout, peer disconnect, server shutdown, and, as
the last resort, connection destruction. `clean` is `#!cpp true` only when the
provider finished normally and every write succeeded. Release the data source
here.

## Connection lifecycle

- **Keep-alive.** After a cleanly finished stream the connection serves the
  next sequential request as usual.
- **Pipelining.** Bytes that arrive while a streamed (or deferred) response is
  still in progress are not served: the current response finishes correctly
  and the connection then closes. Clients that pipeline must reconnect;
  sequential keep-alive clients are unaffected.
- **Peer disconnect.** Crow keeps a read armed while streaming, so a client
  that goes away aborts the transfer even while the provider is idle.
- **Deferred end().** Installing a provider and calling `#!cpp res.end()`
  later, from any thread, is supported; finalization always runs on the
  connection's executor.
- **HEAD** responds with `Transfer-Encoding: chunked`, no `Content-Length`,
  and no body; the provider is not invoked and the completion handler reports
  `clean == true`. **1xx/204/304** suppress the provider with correct bodyless
  framing. **HTTP/1.0** clients receive a finite `505` (chunked coding needs
  HTTP/1.1).
- **Framing.** A response has exactly one body source (the one configured
  last wins), and the wire carries exactly one `Transfer-Encoding: chunked`
  and no `Content-Length`, regardless of headers set around the provider.

## Time and size bounds

- Every socket write of a stream runs under the connection timeout
  (`#!cpp app.timeout(...)`): a client that stops reading trips it and the
  transfer is aborted unclean.
- The wait for a provider is unlimited by default: an idle provider is normal
  for long-lived streams. `#!cpp app.stream_idle_timeout(seconds)` (0 by
  default, at most 255 seconds) aborts a stream whose provider stays silent
  longer than the limit. It cannot interrupt a synchronous provider that
  blocks the connection's executor.
- `#!cpp app.max_stream_chunk_size(bytes)` (16 MiB by default, 0 disables the
  cap) limits a single chunk; a larger chunk aborts the transfer. Together
  with the write deadline it bounds each write: a chunk must be written
  within `app.timeout(...)` seconds, so very large chunks over slow links
  need a larger timeout or smaller chunks.

## Server shutdown

Stopping the server aborts active streams: the provider is released, the
completion handler reports `clean == false` exactly once, and a late
`#!cpp end()` from an application thread is a safe no-op release. Providers
and handlers should not throw; an exception that escapes is logged and treated
as an abort.

A response belongs to the connection once `#!cpp end()` has been called:
calling any setter, `#!cpp write()`, or a second `#!cpp end()` on it after
that point is not supported.
