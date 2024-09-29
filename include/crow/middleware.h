#pragma once

#include "crow/http_request.h"
#include "crow/http_response.h"
#include "crow/utility.h"

#include <tuple>
#include <type_traits>
#include <iostream>
#include <utility>

namespace crow // NOTE: Already documented in "crow/app.h"
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

        template<typename MW>
        struct is_middleware_global
        {
            template<typename C>
            static std::false_type f(typename check_global_call_false<MW>::template get<C>*);

            template<typename C>
            static std::true_type f(...);

            static const bool value = decltype(f<MW>(nullptr))::value;
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


        template<typename CallCriteria,
                 int N, typename Context, typename Container>
        typename std::enable_if<(N < std::tuple_size<typename std::remove_reference<Container>::type>::value), bool>::type
          middleware_call_helper(const CallCriteria& cc, Container& middlewares, request& req, response& res, Context& ctx)
        {

            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;

            if (!cc.template enabled<CurrentMW>(N))
            {
                return middleware_call_helper<CallCriteria, N + 1, Context, Container>(cc, middlewares, req, res, ctx);
            }

            using parent_context_t = typename Context::template partial<N - 1>;
            before_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            if (res.is_completed())
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
                return true;
            }

            if (middleware_call_helper<CallCriteria, N + 1, Context, Container>(cc, middlewares, req, res, ctx))
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
                return true;
            }

            return false;
        }

        template<typename CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N >= std::tuple_size<typename std::remove_reference<Container>::type>::value), bool>::type
          middleware_call_helper(const CallCriteria& /*cc*/, Container& /*middlewares*/, request& /*req*/, response& /*res*/, Context& /*ctx*/)
        {
            return false;
        }

        template<typename CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N < 0)>::type
          after_handlers_call_helper(const CallCriteria& /*cc*/, Container& /*middlewares*/, Context& /*context*/, request& /*req*/, response& /*res*/)
        {
        }

        template<typename CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N == 0)>::type after_handlers_call_helper(const CallCriteria& cc, Container& middlewares, Context& ctx, request& req, response& res)
        {
            using parent_context_t = typename Context::template partial<N - 1>;
            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;
            if (cc.template enabled<CurrentMW>(N))
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            }
        }

        template<typename CallCriteria, int N, typename Context, typename Container>
        typename std::enable_if<(N > 0)>::type after_handlers_call_helper(const CallCriteria& cc, Container& middlewares, Context& ctx, request& req, response& res)
        {
            using parent_context_t = typename Context::template partial<N - 1>;
            using CurrentMW = typename std::tuple_element<N, typename std::remove_reference<Container>::type>::type;
            if (cc.template enabled<CurrentMW>(N))
            {
                after_handler_call<CurrentMW, Context, parent_context_t>(std::get<N>(middlewares), req, res, ctx, static_cast<parent_context_t&>(ctx));
            }
            after_handlers_call_helper<CallCriteria, N - 1, Context, Container>(cc, middlewares, ctx, req, res);
        }

        // A CallCriteria that accepts only global middleware
        struct middleware_call_criteria_only_global
        {
            template<typename MW>
            constexpr bool enabled(int) const
            {
                return is_middleware_global<MW>::value;
            }
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

        template<bool Reversed>
        struct middleware_call_criteria_dynamic
        {};

        template<>
        struct middleware_call_criteria_dynamic<false>
        {
            middleware_call_criteria_dynamic(const std::vector<int>& indices_):
              indices(indices_), slider(0) {}

            template<typename>
            bool enabled(int mw_index) const
            {
                if (slider < int(indices.size()) && indices[slider] == mw_index)
                {
                    slider++;
                    return true;
                }
                return false;
            }

        private:
            const std::vector<int>& indices;
            mutable int slider;
        };

        template<>
        struct middleware_call_criteria_dynamic<true>
        {
            middleware_call_criteria_dynamic(const std::vector<int>& indices_):
              indices(indices_), slider(int(indices_.size()) - 1) {}

            template<typename>
            bool enabled(int mw_index) const
            {
                if (slider >= 0 && indices[slider] == mw_index)
                {
                    slider--;
                    return true;
                }
                return false;
            }

        private:
            const std::vector<int>& indices;
            mutable int slider;
        };

    } // namespace detail
} // namespace crow
