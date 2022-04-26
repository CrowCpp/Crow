#pragma once

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/utility.h"

#include <tuple>
#include <type_traits>
#include <iostream>
#include <utility>

namespace crow
{

    /// Local middleware should extend ILocalMiddleware
    struct ILocalMiddleware
    {
        using call_global = std::false_type;
    };

    namespace detail
    {
        template<typename MW>
        struct check_before_handle_arity_3_const
        {
            template<typename T, void (T::*)(request&, response&, typename MW::context&) const = &T::before_handle>
            struct get
            {};
        };

        template<typename MW>
        struct check_before_handle_arity_3
        {
            template<typename T, void (T::*)(request&, response&, typename MW::context&) = &T::before_handle>
            struct get
            {};
        };

        template<typename MW>
        struct check_after_handle_arity_3_const
        {
            template<typename T, void (T::*)(request&, response&, typename MW::context&) const = &T::after_handle>
            struct get
            {};
        };

        template<typename MW>
        struct check_after_handle_arity_3
        {
            template<typename T, void (T::*)(request&, response&, typename MW::context&) = &T::after_handle>
            struct get
            {};
        };

        template<typename MW>
        struct check_global_call_false
        {
            template<typename T, typename std::enable_if<T::call_global::value == false, bool>::type = true>
            struct get
            {};
        };

        template<typename T>
        struct is_before_handle_arity_3_impl
        {
            template<typename C>
            static std::true_type f(typename check_before_handle_arity_3_const<T>::template get<C>*);

            template<typename C>
            static std::true_type f(typename check_before_handle_arity_3<T>::template get<C>*);

            template<typename C>
            static std::false_type f(...);

        public:
            static const bool value = decltype(f<T>(nullptr))::value;
        };

        template<typename T>
        struct is_after_handle_arity_3_impl
        {
            template<typename C>
            static std::true_type f(typename check_after_handle_arity_3_const<T>::template get<C>*);

            template<typename C>
            static std::true_type f(typename check_after_handle_arity_3<T>::template get<C>*);

            template<typename C>
            static std::false_type f(...);

        public:
            static constexpr bool value = decltype(f<T>(nullptr))::value;
        };

        template<typename MW, typename Context, typename ParentContext>
        typename std::enable_if<!is_before_handle_arity_3_impl<MW>::value>::type
          before_handler_call(MW& mw, request& req, response& res, Context& ctx, ParentContext& /*parent_ctx*/)
        {
            mw.before_handle(req, res, ctx.template get<MW>(), ctx);
        }

        template<typename MW, typename Context, typename ParentContext>
        typename std::enable_if<is_before_handle_arity_3_impl<MW>::value>::type
          before_handler_call(MW& mw, request& req, response& res, Context& ctx, ParentContext& /*parent_ctx*/)
        {
            mw.before_handle(req, res, ctx.template get<MW>());
        }

        template<typename MW, typename Context, typename ParentContext>
        typename std::enable_if<!is_after_handle_arity_3_impl<MW>::value>::type
          after_handler_call(MW& mw, request& req, response& res, Context& ctx, ParentContext& /*parent_ctx*/)
        {
            mw.after_handle(req, res, ctx.template get<MW>(), ctx);
        }

        template<typename MW, typename Context, typename ParentContext>
        typename std::enable_if<is_after_handle_arity_3_impl<MW>::value>::type
          after_handler_call(MW& mw, request& req, response& res, Context& ctx, ParentContext& /*parent_ctx*/)
        {
            mw.after_handle(req, res, ctx.template get<MW>());
        }


        template<template<typename QueryMW> class CallCriteria, // Checks if QueryMW should be called in this context
                 int N, typename Context, typename Container>
        typename std::enable_if<(N < std::tuple_size<typename std::remove_reference<Container>::type>::value), bool>::type
          middleware_call_helper(Container& middlewares, request& req, response& res, Context& ctx)
        {

            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;

            if (!CallCriteria<CurrentMW>::value)
            {
                return middleware_call_helper<CallCriteria, N + 1, Context, Container>(middlewares, req, res, ctx);
            }

            using parent_context_t = typename Context::template partial<N - 1>;
            before_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            if (res.is_completed())
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
                return true;
            }

            if (middleware_call_helper<CallCriteria, N + 1, Context, Container>(middlewares, req, res, ctx))
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
                return true;
            }

            return false;
        }

        template<template<typename QueryMW> class CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N >= std::tuple_size<typename std::remove_reference<Container>::type>::value), bool>::type
          middleware_call_helper(Container& /*middlewares*/, request& /*req*/, response& /*res*/, Context& /*ctx*/)
        {
            return false;
        }

        template<template<typename QueryMW> class CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N < 0)>::type
          after_handlers_call_helper(Container& /*middlewares*/, Context& /*context*/, request& /*req*/, response& /*res*/)
        {
        }

        template<template<typename QueryMW> class CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N == 0)>::type after_handlers_call_helper(Container& middlewares, Context& ctx, request& req, response& res)
        {
            using parent_context_t = typename Context::template partial<N - 1>;
            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;
            if (CallCriteria<CurrentMW>::value)
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            }
        }

        template<template<typename QueryMW> class CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N > 0)>::type after_handlers_call_helper(Container& middlewares, Context& ctx, request& req, response& res)
        {
            using parent_context_t = typename Context::template partial<N - 1>;
            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;
            if (CallCriteria<CurrentMW>::value)
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            }
            after_handlers_call_helper<CallCriteria, N - 1, Context, Container>(middlewares, ctx, req, res);
        }

        // A CallCriteria that accepts only global middleware
        template<typename MW>
        struct middleware_call_criteria_only_global
        {
            template<typename C>
            static std::false_type f(typename check_global_call_false<MW>::template get<C>*);

            template<typename C>
            static std::true_type f(...);

            static const bool value = decltype(f<MW>(nullptr))::value;
        };

        template<typename F, typename... Args>
        typename std::enable_if<black_magic::CallHelper<F, black_magic::S<Args...>>::value, void>::type
          wrapped_handler_call(crow::request& /*req*/, crow::response& res, const F& f, Args&&... args)
        {
            static_assert(!std::is_same<void, decltype(f(std::declval<Args>()...))>::value,
                          "Handler function cannot have void return type; valid return types: string, int, crow::response, crow::returnable");

            res = crow::response(f(std::forward<Args>(args)...));
            res.end();
        }

        template<typename F, typename... Args>
        typename std::enable_if<
          !black_magic::CallHelper<F, black_magic::S<Args...>>::value &&
            black_magic::CallHelper<F, black_magic::S<crow::request&, Args...>>::value,
          void>::type
          wrapped_handler_call(crow::request& req, crow::response& res, const F& f, Args&&... args)
        {
            static_assert(!std::is_same<void, decltype(f(std::declval<crow::request>(), std::declval<Args>()...))>::value,
                          "Handler function cannot have void return type; valid return types: string, int, crow::response, crow::returnable");

            res = crow::response(f(req, std::forward<Args>(args)...));
            res.end();
        }

        template<typename F, typename... Args>
        typename std::enable_if<
          !black_magic::CallHelper<F, black_magic::S<Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<crow::request&, Args...>>::value &&
            black_magic::CallHelper<F, black_magic::S<crow::response&, Args...>>::value,
          void>::type
          wrapped_handler_call(crow::request& /*req*/, crow::response& res, const F& f, Args&&... args)
        {
            static_assert(std::is_same<void, decltype(f(std::declval<crow::response&>(), std::declval<Args>()...))>::value,
                          "Handler function with response argument should have void return type");

            f(res, std::forward<Args>(args)...);
        }

        template<typename F, typename... Args>
        typename std::enable_if<
          !black_magic::CallHelper<F, black_magic::S<Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<crow::request&, Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<crow::response&, Args...>>::value &&
            black_magic::CallHelper<F, black_magic::S<const crow::request&, crow::response&, Args...>>::value,
          void>::type
          wrapped_handler_call(crow::request& req, crow::response& res, const F& f, Args&&... args)
        {
            static_assert(std::is_same<void, decltype(f(std::declval<crow::request&>(), std::declval<crow::response&>(), std::declval<Args>()...))>::value,
                          "Handler function with response argument should have void return type");

            f(req, res, std::forward<Args>(args)...);
        }

        // wrapped_handler_call transparently wraps a handler call behind (req, res, args...)
        template<typename F, typename... Args>
        typename std::enable_if<
          !black_magic::CallHelper<F, black_magic::S<Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<crow::request&, Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<crow::response&, Args...>>::value &&
            !black_magic::CallHelper<F, black_magic::S<const crow::request&, crow::response&, Args...>>::value,
          void>::type
          wrapped_handler_call(crow::request& req, crow::response& res, const F& f, Args&&... args)
        {
            static_assert(std::is_same<void, decltype(f(std::declval<crow::request&>(), std::declval<crow::response&>(), std::declval<Args>()...))>::value,
                          "Handler function with response argument should have void return type");

            f(req, res, std::forward<Args>(args)...);
        }

        template<typename F, typename App, typename... Middlewares>
        struct handler_middleware_wrapper
        {
            // CallCriteria bound to the current Middlewares pack
            template<typename MW>
            struct middleware_call_criteria
            {
                static constexpr bool value = black_magic::has_type<MW, std::tuple<Middlewares...>>::value;
            };

            template<typename... Args>
            void operator()(crow::request& req, crow::response& res, Args&&... args) const
            {
                auto& ctx = *reinterpret_cast<typename App::context_t*>(req.middleware_context);
                auto& container = *reinterpret_cast<typename App::mw_container_t*>(req.middleware_container);

                auto glob_completion_handler = std::move(res.complete_request_handler_);
                res.complete_request_handler_ = [] {};

                middleware_call_helper<middleware_call_criteria,
                                       0, typename App::context_t, typename App::mw_container_t>(container, req, res, ctx);

                if (res.completed_)
                {
                    glob_completion_handler();
                    return;
                }

                res.complete_request_handler_ = [&ctx, &container, &req, &res, &glob_completion_handler] {
                    after_handlers_call_helper<
                      middleware_call_criteria,
                      std::tuple_size<typename App::mw_container_t>::value - 1,
                      typename App::context_t,
                      typename App::mw_container_t>(container, ctx, req, res);
                    glob_completion_handler();
                };

                wrapped_handler_call(req, res, f, std::forward<Args>(args)...);
            }

            F f;
        };

        template<typename Route, typename App, typename... Middlewares>
        struct handler_call_bridge
        {
            template<typename MW>
            using check_app_contains = typename black_magic::has_type<MW, typename App::mw_container_t>;

            static_assert(black_magic::all_true<(std::is_base_of<crow::ILocalMiddleware, Middlewares>::value)...>::value,
                          "Local middleware has to inherit crow::ILocalMiddleware");

            static_assert(black_magic::all_true<(check_app_contains<Middlewares>::value)...>::value,
                          "Local middleware has to be listed in app middleware");

            template<typename F>
            void operator()(F&& f) const
            {
                auto wrapped = handler_middleware_wrapper<F, App, Middlewares...>{std::forward<F>(f)};
                tptr->operator()(std::move(wrapped));
            }

            Route* tptr;
        };

    } // namespace detail
} // namespace crow
