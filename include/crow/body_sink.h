#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace crow
{
    struct request;

    /// Destination for request body bytes as they arrive from the parser.
    ///
    /// `write()`/`finish()` run on the connection's io thread, at headers-complete,
    /// before the `Host` check, before middleware, and before `100 Continue`. A
    /// thrown exception or a `false` return is a 500 with the connection closed;
    /// the route handler never runs. If `finish()` was never called, the body did
    /// not arrive in full (client disconnect, over-limit body, or a write failure).
    struct BodySink
    {
        virtual ~BodySink() = default;
        virtual bool write(const char* data, std::size_t length) = 0;
        virtual bool finish() = 0;
    };

    /// `req` carries method, url, headers and `remote_ip_address` (set before the
    /// factory runs); `req.body` and `req.body_sink` are not yet populated.
    /// Returning `nullptr` keeps the body in `req.body` instead of diverting it.
    using BodySinkFactory = std::function<std::unique_ptr<BodySink>(const request&)>;
} // namespace crow
