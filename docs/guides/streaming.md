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

## Completion handler

To find out how the transfer ended (to release the source of the data, or to log
a failure), set a handler that is called once after the body has been written:

```cpp
res.set_chunked_completion_handler([](bool clean) {
    if (!clean)
        CROW_LOG_WARNING << "chunked transfer did not finish cleanly";
});
```

`clean` is `#!cpp true` when the provider finished normally
(`#!cpp crow::chunk_result::done`, or `#!cpp false` from the `bool` provider)
and every write succeeded; it is `#!cpp false` when the provider aborted or a
write error occurred. The handler runs on the connection's thread, after the
last write and before the response is finalized.

## Notes

!!! note

    The provider runs on the connection's thread while the response is being
    written, so a provider that blocks keeps that thread busy for the whole
    transfer.

!!! note

    The connection deadline is cancelled for the duration of the transfer.
    Without that, a body that takes longer to produce than the timeout would be
    cut short by the connection being closed.

!!! note

    A response to a `HEAD` request never calls the provider: the headers are sent
    and the body is skipped.
