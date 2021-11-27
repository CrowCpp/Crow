#pragma once
#include "crow/http_request.h"
#include "crow/http_response.h"

namespace crow
{

    struct UTF8
    {
        struct context
        {};

        void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/)
        {}

        void after_handle(request& /*req*/, response& res, context& /*ctx*/)
        {
            if (get_header_value(res.headers, "Content-Type").empty())
            {
                res.set_header("Content-Type", "text/plain; charset=utf-8");
            }
        }
    };

} // namespace crow
