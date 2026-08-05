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
allowed and sends nothing, which is handy when the source of the data has
produced no bytes yet.

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
