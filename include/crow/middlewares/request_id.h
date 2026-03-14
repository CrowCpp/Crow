#pragma once

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/utility.h"

namespace crow
{
namespace middleware
{

struct RequestId
{
    struct context
    {
        std::string request_id;
    };

    void before_handle(request& req, response& /*res*/, context& ctx)
    {
        const std::string& x_request_id = req.get_header_value("X-Request-Id");
        if (!x_request_id.empty())
        {
            ctx.request_id = x_request_id;
        }
        else
        {
            ctx.request_id = utility::random_alphanum(16);
        }
    }

    void after_handle(request& /*req*/, response& res, context& ctx)
    {
        res.add_header("X-Request-Id", ctx.request_id);
    }
};

} // namespace middleware
} // namespace crow
