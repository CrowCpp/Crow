#pragma once
#include "crow.h"
#include <vector>

using namespace crow;

struct NullMiddleware
{
    struct context
    {};

    template<typename AllContext>
    void before_handle(request&, response&, context&, AllContext&)
    {}

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext&)
    {}
};

struct NullSimpleMiddleware
{
    struct context
    {};

    void before_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}

    void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/) {}
};

struct IntSettingMiddleware
{
    struct context
    {
        int val;
    };

    template<typename AllContext>
    void before_handle(request&, response&, context& ctx, AllContext&)
    {
        ctx.val = 1;
    }

    template<typename AllContext>
    void after_handle(request&, response&, context& ctx, AllContext&)
    {
        ctx.val = 2;
    }
};

static std::vector<std::string> test_middleware_context_vector;

struct empty_type
{};

template<bool Local>
struct FirstMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {
        std::vector<string> v;
    };

    void before_handle(request& /*req*/, response& /*res*/, context& ctx)
    {
        ctx.v.push_back("1 before");
    }

    void after_handle(request& /*req*/, response& /*res*/, context& ctx)
    {
        ctx.v.push_back("1 after");
        test_middleware_context_vector = ctx.v;
    }
};

template<bool Local>
struct SecondMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {};
    template<typename AllContext>
    void before_handle(request& req, response& res, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("2 before");
        if (req.url.find("/break") != std::string::npos) res.end();
    }

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("2 after");
    }
};

template<bool Local>
struct ThirdMW : public std::conditional<Local, crow::ILocalMiddleware, empty_type>::type
{
    struct context
    {};
    template<typename AllContext>
    void before_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("3 before");
    }

    template<typename AllContext>
    void after_handle(request&, response&, context&, AllContext& all_ctx)
    {
        all_ctx.template get<FirstMW<Local>>().v.push_back("3 after");
    }
};


struct LocalSecretMiddleware : crow::ILocalMiddleware
{
    struct context
    {};

    void before_handle(request& /*req*/, response& res, context& /*ctx*/)
    {
        res.code = 403;
        res.end();
    }

    void after_handle(request& /*req*/, response& /*res*/, context& /*ctx*/)
    {}
};
