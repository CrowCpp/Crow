#pragma once

#include <cstdint>
#include <utility>
#include <tuple>
#include <unordered_map>
#include <memory>
#include <boost/lexical_cast.hpp>
#include <vector>

#include "crow/common.h"
#include "crow/http_response.h"
#include "crow/http_request.h"
#include "crow/utility.h"
#include "crow/logging.h"
#include "crow/websocket.h"
#include "crow/mustache.h"
#include "crow/middleware.h"

namespace crow
{

    constexpr const uint16_t INVALID_BP_ID{((uint16_t)-1)};

    /// A base class for all rules.

    ///
    /// Used to provide a common interface for code dealing with different types of rules.<br>
    /// A Rule provides a URL, allowed HTTP methods, and handlers.
    class BaseRule
    {
    public:
        BaseRule(std::string rule):
          rule_(std::move(rule))
        {}

        virtual ~BaseRule()
        {}

        virtual void validate() = 0;
        std::unique_ptr<BaseRule> upgrade()
        {
            if (rule_to_upgrade_)
                return std::move(rule_to_upgrade_);
            return {};
        }

        virtual void handle(request&, response&, const routing_params&) = 0;
        virtual void handle_upgrade(const request&, response& res, SocketAdaptor&&)
        {
            res = response(404);
            res.end();
        }
#ifdef CROW_ENABLE_SSL
        virtual void handle_upgrade(const request&, response& res, SSLAdaptor&&)
        {
            res = response(404);
            res.end();
        }
#endif

        uint32_t get_methods()
        {
            return methods_;
        }

        template<typename F>
        void foreach_method(F f)
        {
            for (uint32_t method = 0, method_bit = 1; method < static_cast<uint32_t>(HTTPMethod::InternalMethodCount); method++, method_bit <<= 1)
            {
                if (methods_ & method_bit)
                    f(method);
            }
        }


        std::string custom_templates_base;

        const std::string& rule() { return rule_; }

    protected:
        uint32_t methods_{1 << static_cast<int>(HTTPMethod::Get)};

        std::string rule_;
        std::string name_;

        std::unique_ptr<BaseRule> rule_to_upgrade_;

        friend class Router;
        friend class Blueprint;
        template<typename T>
        friend struct RuleParameterTraits;
    };


    namespace detail
    {
        namespace routing_handler_call_helper
        {
            template<typename T, int Pos>
            struct call_pair
            {
                using type = T;
                static const int pos = Pos;
            };

            template<typename H1>
            struct call_params
            {
                H1& handler;
                const routing_params& params;
                request& req;
                response& res;
            };

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename S1, typename S2>
            struct call
            {};

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename... Args1, typename... Args2>
            struct call<F, NInt, NUint, NDouble, NString, black_magic::S<int64_t, Args1...>, black_magic::S<Args2...>>
            {
                void operator()(F cparams)
                {
                    using pushed = typename black_magic::S<Args2...>::template push_back<call_pair<int64_t, NInt>>;
                    call<F, NInt + 1, NUint, NDouble, NString, black_magic::S<Args1...>, pushed>()(cparams);
                }
            };

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename... Args1, typename... Args2>
            struct call<F, NInt, NUint, NDouble, NString, black_magic::S<uint64_t, Args1...>, black_magic::S<Args2...>>
            {
                void operator()(F cparams)
                {
                    using pushed = typename black_magic::S<Args2...>::template push_back<call_pair<uint64_t, NUint>>;
                    call<F, NInt, NUint + 1, NDouble, NString, black_magic::S<Args1...>, pushed>()(cparams);
                }
            };

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename... Args1, typename... Args2>
            struct call<F, NInt, NUint, NDouble, NString, black_magic::S<double, Args1...>, black_magic::S<Args2...>>
            {
                void operator()(F cparams)
                {
                    using pushed = typename black_magic::S<Args2...>::template push_back<call_pair<double, NDouble>>;
                    call<F, NInt, NUint, NDouble + 1, NString, black_magic::S<Args1...>, pushed>()(cparams);
                }
            };

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename... Args1, typename... Args2>
            struct call<F, NInt, NUint, NDouble, NString, black_magic::S<std::string, Args1...>, black_magic::S<Args2...>>
            {
                void operator()(F cparams)
                {
                    using pushed = typename black_magic::S<Args2...>::template push_back<call_pair<std::string, NString>>;
                    call<F, NInt, NUint, NDouble, NString + 1, black_magic::S<Args1...>, pushed>()(cparams);
                }
            };

            template<typename F, int NInt, int NUint, int NDouble, int NString, typename... Args1>
            struct call<F, NInt, NUint, NDouble, NString, black_magic::S<>, black_magic::S<Args1...>>
            {
                void operator()(F cparams)
                {
                    cparams.handler(
                      cparams.req,
                      cparams.res,
                      cparams.params.template get<typename Args1::type>(Args1::pos)...);
                }
            };

            template<typename Func, typename... ArgsWrapped>
            struct Wrapped
            {
                template<typename... Args>
                void set_(Func f, typename std::enable_if<!std::is_same<typename std::tuple_element<0, std::tuple<Args..., void>>::type, const request&>::value, int>::type = 0)
                {
                    handler_ = (
#ifdef CROW_CAN_USE_CPP14
                      [f = std::move(f)]
#else
                      [f]
#endif
                      (const request&, response& res, Args... args) {
                          res = response(f(args...));
                          res.end();
                      });
                }

                template<typename Req, typename... Args>
                struct req_handler_wrapper
                {
                    req_handler_wrapper(Func f):
                      f(std::move(f))
                    {
                    }

                    void operator()(const request& req, response& res, Args... args)
                    {
                        res = response(f(req, args...));
                        res.end();
                    }

                    Func f;
                };

                template<typename... Args>
                void set_(Func f, typename std::enable_if<
                                    std::is_same<typename std::tuple_element<0, std::tuple<Args..., void>>::type, const request&>::value &&
                                      !std::is_same<typename std::tuple_element<1, std::tuple<Args..., void, void>>::type, response&>::value,
                                    int>::type = 0)
                {
                    handler_ = req_handler_wrapper<Args...>(std::move(f));
                    /*handler_ = (
                        [f = std::move(f)]
                        (const request& req, response& res, Args... args){
                             res = response(f(req, args...));
                             res.end();
                        });*/
                }

                template<typename... Args>
                void set_(Func f, typename std::enable_if<
                                    std::is_same<typename std::tuple_element<0, std::tuple<Args..., void>>::type, const request&>::value &&
                                      std::is_same<typename std::tuple_element<1, std::tuple<Args..., void, void>>::type, response&>::value,
                                    int>::type = 0)
                {
                    handler_ = std::move(f);
                }

                template<typename... Args>
                struct handler_type_helper
                {
                    using type = std::function<void(const crow::request&, crow::response&, Args...)>;
                    using args_type = black_magic::S<typename black_magic::promote_t<Args>...>;
                };

                template<typename... Args>
                struct handler_type_helper<const request&, Args...>
                {
                    using type = std::function<void(const crow::request&, crow::response&, Args...)>;
                    using args_type = black_magic::S<typename black_magic::promote_t<Args>...>;
                };

                template<typename... Args>
                struct handler_type_helper<const request&, response&, Args...>
                {
                    using type = std::function<void(const crow::request&, crow::response&, Args...)>;
                    using args_type = black_magic::S<typename black_magic::promote_t<Args>...>;
                };

                typename handler_type_helper<ArgsWrapped...>::type handler_;

                void operator()(request& req, response& res, const routing_params& params)
                {
                    detail::routing_handler_call_helper::call<
                      detail::routing_handler_call_helper::call_params<
                        decltype(handler_)>,
                      0, 0, 0, 0,
                      typename handler_type_helper<ArgsWrapped...>::args_type,
                      black_magic::S<>>()(
                      detail::routing_handler_call_helper::call_params<
                        decltype(handler_)>{handler_, params, req, res});
                }
            };

        } // namespace routing_handler_call_helper
    }     // namespace detail


    class CatchallRule
    {
    public:
        /// @cond SKIP
        CatchallRule() {}

        template<typename Func>
        typename std::enable_if<black_magic::CallHelper<Func, black_magic::S<>>::value, void>::type
          operator()(Func&& f)
        {
            static_assert(!std::is_same<void, decltype(f())>::value,
                          "Handler function cannot have void return type; valid return types: string, int, crow::response, crow::returnable");

            handler_ = (
#ifdef CROW_CAN_USE_CPP14
              [f = std::move(f)]
#else
              [f]
#endif
              (const request&, response& res) {
                  res = response(f());
                  res.end();
              });
        }

        template<typename Func>
        typename std::enable_if<
          !black_magic::CallHelper<Func, black_magic::S<>>::value &&
            black_magic::CallHelper<Func, black_magic::S<crow::request>>::value,
          void>::type
          operator()(Func&& f)
        {
            static_assert(!std::is_same<void, decltype(f(std::declval<crow::request>()))>::value,
                          "Handler function cannot have void return type; valid return types: string, int, crow::response, crow::returnable");

            handler_ = (
#ifdef CROW_CAN_USE_CPP14
              [f = std::move(f)]
#else
              [f]
#endif
              (const crow::request& req, crow::response& res) {
                  res = response(f(req));
                  res.end();
              });
        }

        template<typename Func>
        typename std::enable_if<
          !black_magic::CallHelper<Func, black_magic::S<>>::value &&
            !black_magic::CallHelper<Func, black_magic::S<crow::request>>::value &&
            black_magic::CallHelper<Func, black_magic::S<crow::response&>>::value,
          void>::type
          operator()(Func&& f)
        {
            static_assert(std::is_same<void, decltype(f(std::declval<crow::response&>()))>::value,
                          "Handler function with response argument should have void return type");
            handler_ = (
#ifdef CROW_CAN_USE_CPP14
              [f = std::move(f)]
#else
              [f]
#endif
              (const crow::request&, crow::response& res) {
                  f(res);
              });
        }

        template<typename Func>
        typename std::enable_if<
          !black_magic::CallHelper<Func, black_magic::S<>>::value &&
            !black_magic::CallHelper<Func, black_magic::S<crow::request>>::value &&
            !black_magic::CallHelper<Func, black_magic::S<crow::response&>>::value,
          void>::type
          operator()(Func&& f)
        {
            static_assert(std::is_same<void, decltype(f(std::declval<crow::request>(), std::declval<crow::response&>()))>::value,
                          "Handler function with response argument should have void return type");

            handler_ = std::move(f);
        }
        /// @endcond
        bool has_handler()
        {
            return (handler_ != nullptr);
        }

    protected:
        friend class Router;

    private:
        std::function<void(const crow::request&, crow::response&)> handler_;
    };


    /// A rule dealing with websockets.

    ///
    /// Provides the interface for the user to put in the necessary handlers for a websocket to work.
    class WebSocketRule : public BaseRule
    {
        using self_t = WebSocketRule;

    public:
        WebSocketRule(std::string rule):
          BaseRule(std::move(rule))
        {}

        void validate() override
        {}

        void handle(request&, response& res, const routing_params&) override
        {
            res = response(404);
            res.end();
        }

        void handle_upgrade(const request& req, response&, SocketAdaptor&& adaptor) override
        {
            new crow::websocket::Connection<SocketAdaptor>(req, std::move(adaptor), open_handler_, message_handler_, close_handler_, error_handler_, accept_handler_);
        }
#ifdef CROW_ENABLE_SSL
        void handle_upgrade(const request& req, response&, SSLAdaptor&& adaptor) override
        {
            new crow::websocket::Connection<SSLAdaptor>(req, std::move(adaptor), open_handler_, message_handler_, close_handler_, error_handler_, accept_handler_);
        }
#endif

        template<typename Func>
        self_t& onopen(Func f)
        {
            open_handler_ = f;
            return *this;
        }

        template<typename Func>
        self_t& onmessage(Func f)
        {
            message_handler_ = f;
            return *this;
        }

        template<typename Func>
        self_t& onclose(Func f)
        {
            close_handler_ = f;
            return *this;
        }

        template<typename Func>
        self_t& onerror(Func f)
        {
            error_handler_ = f;
            return *this;
        }

        template<typename Func>
        self_t& onaccept(Func f)
        {
            accept_handler_ = f;
            return *this;
        }

    protected:
        std::function<void(crow::websocket::connection&)> open_handler_;
        std::function<void(crow::websocket::connection&, const std::string&, bool)> message_handler_;
        std::function<void(crow::websocket::connection&, const std::string&)> close_handler_;
        std::function<void(crow::websocket::connection&)> error_handler_;
        std::function<bool(const crow::request&)> accept_handler_;
    };

    /// Allows the user to assign parameters using functions.

    ///
    /// `rule.name("name").methods(HTTPMethod::POST)`
    template<typename T>
    struct RuleParameterTraits
    {
        using self_t = T;
        WebSocketRule& websocket()
        {
            auto p = new WebSocketRule(static_cast<self_t*>(this)->rule_);
            static_cast<self_t*>(this)->rule_to_upgrade_.reset(p);
            return *p;
        }

        self_t& name(std::string name) noexcept
        {
            static_cast<self_t*>(this)->name_ = std::move(name);
            return static_cast<self_t&>(*this);
        }

        self_t& methods(HTTPMethod method)
        {
            static_cast<self_t*>(this)->methods_ = 1 << static_cast<int>(method);
            return static_cast<self_t&>(*this);
        }

        template<typename... MethodArgs>
        self_t& methods(HTTPMethod method, MethodArgs... args_method)
        {
            methods(args_method...);
            static_cast<self_t*>(this)->methods_ |= 1 << static_cast<int>(method);
            return static_cast<self_t&>(*this);
        }
    };

    /// A rule that can change its parameters during runtime.
    class DynamicRule : public BaseRule, public RuleParameterTraits<DynamicRule>
    {
    public:
        DynamicRule(std::string rule):
          BaseRule(std::move(rule))
        {}

        void validate() override
        {
            if (!erased_handler_)
            {
                throw std::runtime_error(name_ + (!name_.empty() ? ": " : "") + "no handler for url " + rule_);
            }
        }

        void handle(request& req, response& res, const routing_params& params) override
        {
            if (!custom_templates_base.empty())
                mustache::set_base(custom_templates_base);
            else if (mustache::detail::get_template_base_directory_ref() != "templates")
                mustache::set_base("templates");
            erased_handler_(req, res, params);
        }

        template<typename Func>
        void operator()(Func f)
        {
#ifdef CROW_MSVC_WORKAROUND
            using function_t = utility::function_traits<decltype(&Func::operator())>;
#else
            using function_t = utility::function_traits<Func>;
#endif
            erased_handler_ = wrap(std::move(f), black_magic::gen_seq<function_t::arity>());
        }

        // enable_if Arg1 == request && Arg2 == response
        // enable_if Arg1 == request && Arg2 != resposne
        // enable_if Arg1 != request
#ifdef CROW_MSVC_WORKAROUND
        template<typename Func, size_t... Indices>
#else
        template<typename Func, unsigned... Indices>
#endif
        std::function<void(request&, response&, const routing_params&)>
          wrap(Func f, black_magic::seq<Indices...>)
        {
#ifdef CROW_MSVC_WORKAROUND
            using function_t = utility::function_traits<decltype(&Func::operator())>;
#else
            using function_t = utility::function_traits<Func>;
#endif
            if (!black_magic::is_parameter_tag_compatible(
                  black_magic::get_parameter_tag_runtime(rule_.c_str()),
                  black_magic::compute_parameter_tag_from_args_list<
                    typename function_t::template arg<Indices>...>::value))
            {
                throw std::runtime_error("route_dynamic: Handler type is mismatched with URL parameters: " + rule_);
            }
            auto ret = detail::routing_handler_call_helper::Wrapped<Func, typename function_t::template arg<Indices>...>();
            ret.template set_<
              typename function_t::template arg<Indices>...>(std::move(f));
            return ret;
        }

        template<typename Func>
        void operator()(std::string name, Func&& f)
        {
            name_ = std::move(name);
            (*this).template operator()<Func>(std::forward(f));
        }

    private:
        std::function<void(request&, response&, const routing_params&)> erased_handler_;
    };

    /// Default rule created when CROW_ROUTE is called.
    template<typename... Args>
    class TaggedRule : public BaseRule, public RuleParameterTraits<TaggedRule<Args...>>
    {
    public:
        using self_t = TaggedRule<Args...>;

        TaggedRule(std::string rule):
          BaseRule(std::move(rule))
        {}

        void validate() override
        {
            if (!handler_)
            {
                throw std::runtime_error(name_ + (!name_.empty() ? ": " : "") + "no handler for url " + rule_);
            }
        }

        template<typename Func>
        void operator()(Func&& f)
        {
            handler_ = (
#ifdef CROW_CAN_USE_CPP14
              [f = std::move(f)]
#else
              [f]
#endif
              (crow::request& req, crow::response& res, Args... args) {
                  detail::wrapped_handler_call(req, res, f, std::forward<Args>(args)...);
              });
        }

        template<typename Func>
        void operator()(std::string name, Func&& f)
        {
            name_ = std::move(name);
            (*this).template operator()<Func>(std::forward(f));
        }

        void handle(request& req, response& res, const routing_params& params) override
        {
            if (!custom_templates_base.empty())
                mustache::set_base(custom_templates_base);
            else if (mustache::detail::get_template_base_directory_ref() != mustache::detail::get_global_template_base_directory_ref())
                mustache::set_base(mustache::detail::get_global_template_base_directory_ref());

            detail::routing_handler_call_helper::call<
              detail::routing_handler_call_helper::call_params<decltype(handler_)>,
              0, 0, 0, 0,
              black_magic::S<Args...>,
              black_magic::S<>>()(
              detail::routing_handler_call_helper::call_params<decltype(handler_)>{handler_, params, req, res});
        }

        /// Enable local middleware for this handler
        template<typename App, typename... Middlewares>
        crow::detail::handler_call_bridge<TaggedRule<Args...>, App, Middlewares...>
          middlewares()
        {
            // the handler_call_bridge allows the functor to be placed directly after this function
            // instead of wrapping it with more parentheses
            return {this};
        }

    private:
        std::function<void(crow::request&, crow::response&, Args...)> handler_;
    };

    const int RULE_SPECIAL_REDIRECT_SLASH = 1;


    /// A search tree.
    class Trie
    {
    public:
        struct Node
        {
            uint16_t rule_index{};
            // Assign the index to the maximum 32 unsigned integer value by default so that any other number (specifically 0) is a valid BP id.
            uint16_t blueprint_index{INVALID_BP_ID};
            std::string key;
            ParamType param = ParamType::MAX; // MAX = No param.
            std::vector<Node*> children;

            bool IsSimpleNode() const
            {
                return !rule_index &&
                       blueprint_index == INVALID_BP_ID &&
                       children.size() < 2 &&
                       param == ParamType::MAX &&
                       std::all_of(std::begin(children), std::end(children), [](Node* x) {
                           return x->param == ParamType::MAX;
                       });
            }
        };


        Trie()
        {}

        /// Check whether or not the trie is empty.
        bool is_empty()
        {
            return head_.children.empty();
        }

        void optimize()
        {
            for (auto child : head_.children)
            {
                optimizeNode(child);
            }
        }


    private:
        void optimizeNode(Node* node)
        {
            if (node->children.empty())
                return;
            if (node->IsSimpleNode())
            {
                Node* child_temp = node->children[0];
                node->key = node->key + child_temp->key;
                node->rule_index = child_temp->rule_index;
                node->blueprint_index = child_temp->blueprint_index;
                node->children = std::move(child_temp->children);
                delete (child_temp);
                optimizeNode(node);
            }
            else
            {
                for (auto& child : node->children)
                {
                    optimizeNode(child);
                }
            }
        }

        void debug_node_print(Node* node, int level)
        {
            if (node->param != ParamType::MAX)
            {
                switch (node->param)
                {
                    case ParamType::INT:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<int>";
                        break;
                    case ParamType::UINT:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<uint>";
                        break;
                    case ParamType::DOUBLE:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<double>";
                        break;
                    case ParamType::STRING:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<string>";
                        break;
                    case ParamType::PATH:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<path>";
                        break;
                    default:
                        CROW_LOG_DEBUG << std::string(2 * level, ' ') << "<ERROR>";
                        break;
                }
            }
            else
                CROW_LOG_DEBUG << std::string(2 * level, ' ') << node->key;

            for (auto& child : node->children)
            {
                debug_node_print(child, level + 1);
            }
        }

    public:
        void debug_print()
        {
            CROW_LOG_DEBUG << "HEAD";
            for (auto& child : head_.children)
                debug_node_print(child, 1);
        }

        void validate()
        {
            if (!head_.IsSimpleNode())
                throw std::runtime_error("Internal error: Trie header should be simple!");
            optimize();
        }

        //Rule_index, Blueprint_index, routing_params
        std::tuple<uint16_t, std::vector<uint16_t>, routing_params> find(const std::string& req_url, const Node* node = nullptr, unsigned pos = 0, routing_params* params = nullptr, std::vector<uint16_t>* blueprints = nullptr) const
        {
            //start params as an empty struct
            routing_params empty;
            if (params == nullptr)
                params = &empty;
            //same for blueprint vector
            std::vector<uint16_t> MT;
            if (blueprints == nullptr)
                blueprints = &MT;

            uint16_t found{};               //The rule index to be found
            std::vector<uint16_t> found_BP; //The Blueprint indices to be found
            routing_params match_params;    //supposedly the final matched parameters

            //start from the head node
            if (node == nullptr)
                node = &head_;

            auto update_found = [&found, &found_BP, &match_params](std::tuple<uint16_t, std::vector<uint16_t>, routing_params>& ret) {
                found_BP = std::move(std::get<1>(ret));
                if (std::get<0>(ret) && (!found || found > std::get<0>(ret)))
                {
                    found = std::get<0>(ret);
                    match_params = std::move(std::get<2>(ret));
                }
            };

            //if the function was called on a node at the end of the string (the last recursion), return the nodes rule index, and whatever params were passed to the function
            if (pos == req_url.size())
            {
                found_BP = std::move(*blueprints);
                return std::tuple<uint16_t, std::vector<uint16_t>, routing_params>{node->rule_index, *blueprints, *params};
            }

            bool found_fragment = false;

            for (auto& child : node->children)
            {
                if (child->param != ParamType::MAX)
                {
                    if (child->param == ParamType::INT)
                    {
                        char c = req_url[pos];
                        if ((c >= '0' && c <= '9') || c == '+' || c == '-')
                        {
                            char* eptr;
                            errno = 0;
                            long long int value = strtoll(req_url.data() + pos, &eptr, 10);
                            if (errno != ERANGE && eptr != req_url.data() + pos)
                            {
                                found_fragment = true;
                                params->int_params.push_back(value);
                                if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                                auto ret = find(req_url, child, eptr - req_url.data(), params, blueprints);
                                update_found(ret);
                                params->int_params.pop_back();
                                if (!blueprints->empty()) blueprints->pop_back();
                            }
                        }
                    }

                    else if (child->param == ParamType::UINT)
                    {
                        char c = req_url[pos];
                        if ((c >= '0' && c <= '9') || c == '+')
                        {
                            char* eptr;
                            errno = 0;
                            unsigned long long int value = strtoull(req_url.data() + pos, &eptr, 10);
                            if (errno != ERANGE && eptr != req_url.data() + pos)
                            {
                                found_fragment = true;
                                params->uint_params.push_back(value);
                                if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                                auto ret = find(req_url, child, eptr - req_url.data(), params, blueprints);
                                update_found(ret);
                                params->uint_params.pop_back();
                                if (!blueprints->empty()) blueprints->pop_back();
                            }
                        }
                    }

                    else if (child->param == ParamType::DOUBLE)
                    {
                        char c = req_url[pos];
                        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')
                        {
                            char* eptr;
                            errno = 0;
                            double value = strtod(req_url.data() + pos, &eptr);
                            if (errno != ERANGE && eptr != req_url.data() + pos)
                            {
                                found_fragment = true;
                                params->double_params.push_back(value);
                                if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                                auto ret = find(req_url, child, eptr - req_url.data(), params, blueprints);
                                update_found(ret);
                                params->double_params.pop_back();
                                if (!blueprints->empty()) blueprints->pop_back();
                            }
                        }
                    }

                    else if (child->param == ParamType::STRING)
                    {
                        size_t epos = pos;
                        for (; epos < req_url.size(); epos++)
                        {
                            if (req_url[epos] == '/')
                                break;
                        }

                        if (epos != pos)
                        {
                            found_fragment = true;
                            params->string_params.push_back(req_url.substr(pos, epos - pos));
                            if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                            auto ret = find(req_url, child, epos, params, blueprints);
                            update_found(ret);
                            params->string_params.pop_back();
                            if (!blueprints->empty()) blueprints->pop_back();
                        }
                    }

                    else if (child->param == ParamType::PATH)
                    {
                        size_t epos = req_url.size();

                        if (epos != pos)
                        {
                            found_fragment = true;
                            params->string_params.push_back(req_url.substr(pos, epos - pos));
                            if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                            auto ret = find(req_url, child, epos, params, blueprints);
                            update_found(ret);
                            params->string_params.pop_back();
                            if (!blueprints->empty()) blueprints->pop_back();
                        }
                    }
                }

                else
                {
                    const std::string& fragment = child->key;
                    if (req_url.compare(pos, fragment.size(), fragment) == 0)
                    {
                        found_fragment = true;
                        if (child->blueprint_index != INVALID_BP_ID) blueprints->push_back(child->blueprint_index);
                        auto ret = find(req_url, child, pos + fragment.size(), params, blueprints);
                        update_found(ret);
                        if (!blueprints->empty()) blueprints->pop_back();
                    }
                }
            }

            if (!found_fragment)
                found_BP = std::move(*blueprints);

            return std::tuple<uint16_t, std::vector<uint16_t>, routing_params>{found, found_BP, match_params}; //Called after all the recursions have been done
        }

        //This functions assumes any blueprint info passed is valid
        void add(const std::string& url, uint16_t rule_index, unsigned bp_prefix_length = 0, uint16_t blueprint_index = INVALID_BP_ID)
        {
            Node* idx = &head_;

            bool has_blueprint = bp_prefix_length != 0 && blueprint_index != INVALID_BP_ID;

            for (unsigned i = 0; i < url.size(); i++)
            {
                char c = url[i];
                if (c == '<')
                {
                    static struct ParamTraits
                    {
                        ParamType type;
                        std::string name;
                    } paramTraits[] =
                      {
                        {ParamType::INT, "<int>"},
                        {ParamType::UINT, "<uint>"},
                        {ParamType::DOUBLE, "<float>"},
                        {ParamType::DOUBLE, "<double>"},
                        {ParamType::STRING, "<str>"},
                        {ParamType::STRING, "<string>"},
                        {ParamType::PATH, "<path>"},
                      };

                    for (auto& x : paramTraits)
                    {
                        if (url.compare(i, x.name.size(), x.name) == 0)
                        {
                            bool found = false;
                            for (Node* child : idx->children)
                            {
                                if (child->param == x.type)
                                {
                                    idx = child;
                                    i += x.name.size();
                                    found = true;
                                    break;
                                }
                            }
                            if (found)
                                break;

                            auto new_node_idx = new_node(idx);
                            new_node_idx->param = x.type;
                            idx = new_node_idx;
                            i += x.name.size();
                            break;
                        }
                    }

                    i--;
                }
                else
                {
                    //This part assumes the tree is unoptimized (every node has a max 1 character key)
                    bool piece_found = false;
                    for (auto& child : idx->children)
                    {
                        if (child->key[0] == c)
                        {
                            idx = child;
                            piece_found = true;
                            break;
                        }
                    }
                    if (!piece_found)
                    {
                        auto new_node_idx = new_node(idx);
                        new_node_idx->key = c;
                        //The assumption here is that you'd only need to add a blueprint index if the tree didn't have the BP prefix.
                        if (has_blueprint && i == bp_prefix_length)
                            new_node_idx->blueprint_index = blueprint_index;
                        idx = new_node_idx;
                    }
                }
            }

            //check if the last node already has a value (exact url already in Trie)
            if (idx->rule_index)
                throw std::runtime_error("handler already exists for " + url);
            idx->rule_index = rule_index;
        }

        size_t get_size()
        {
            return get_size(&head_);
        }

        size_t get_size(Node* node)
        {
            unsigned size = 5;          //rule_index, blueprint_index, and param
            size += (node->key.size()); //each character in the key is 1 byte
            for (auto child : node->children)
            {
                size += get_size(child);
            }
            return size;
        }


    private:
        Node* new_node(Node* parent)
        {
            auto& children = parent->children;
            children.resize(children.size() + 1);
            children[children.size() - 1] = new Node();
            return children[children.size() - 1];
        }


        Node head_;
    };

    /// A blueprint can be considered a smaller section of a Crow app, specifically where the router is conecerned.

    ///
    /// You can use blueprints to assign a common prefix to rules' prefix, set custom static and template folders, and set a custom catchall route.
    /// You can also assign nest blueprints for maximum Compartmentalization.
    class Blueprint
    {
    public:
        Blueprint(const std::string& prefix):
          prefix_(prefix){};

        Blueprint(const std::string& prefix, const std::string& static_dir):
          prefix_(prefix), static_dir_(static_dir){};

        Blueprint(const std::string& prefix, const std::string& static_dir, const std::string& templates_dir):
          prefix_(prefix), static_dir_(static_dir), templates_dir_(templates_dir){};

        /*
        Blueprint(Blueprint& other)
        {
            prefix_ = std::move(other.prefix_);
            all_rules_ = std::move(other.all_rules_);
        }

        Blueprint(const Blueprint& other)
        {
            prefix_ = other.prefix_;
            all_rules_ = other.all_rules_;
        }
*/
        Blueprint(Blueprint&& value)
        {
            *this = std::move(value);
        }

        Blueprint& operator=(const Blueprint& value) = delete;

        Blueprint& operator=(Blueprint&& value) noexcept
        {
            prefix_ = std::move(value.prefix_);
            all_rules_ = std::move(value.all_rules_);
            catchall_rule_ = std::move(value.catchall_rule_);
            return *this;
        }

        bool operator==(const Blueprint& value)
        {
            return value.prefix() == prefix_;
        }

        bool operator!=(const Blueprint& value)
        {
            return value.prefix() != prefix_;
        }

        std::string prefix() const
        {
            return prefix_;
        }

        std::string static_dir() const
        {
            return static_dir_;
        }

        DynamicRule& new_rule_dynamic(std::string&& rule)
        {
            std::string new_rule = std::move(rule);
            new_rule = '/' + prefix_ + new_rule;
            auto ruleObject = new DynamicRule(new_rule);
            ruleObject->custom_templates_base = templates_dir_;
            all_rules_.emplace_back(ruleObject);

            return *ruleObject;
        }

        template<uint64_t N>
        typename black_magic::arguments<N>::type::template rebind<TaggedRule>& new_rule_tagged(std::string&& rule)
        {
            std::string new_rule = std::move(rule);
            new_rule = '/' + prefix_ + new_rule;
            using RuleT = typename black_magic::arguments<N>::type::template rebind<TaggedRule>;

            auto ruleObject = new RuleT(new_rule);
            ruleObject->custom_templates_base = templates_dir_;
            all_rules_.emplace_back(ruleObject);

            return *ruleObject;
        }

        void register_blueprint(Blueprint& blueprint)
        {
            if (blueprints_.empty() || std::find(blueprints_.begin(), blueprints_.end(), &blueprint) == blueprints_.end())
            {
                apply_blueprint(blueprint);
                blueprints_.emplace_back(&blueprint);
            }
            else
                throw std::runtime_error("blueprint \"" + blueprint.prefix_ + "\" already exists in blueprint \"" + prefix_ + '\"');
        }


        CatchallRule& catchall_rule()
        {
            return catchall_rule_;
        }

    private:
        void apply_blueprint(Blueprint& blueprint)
        {

            blueprint.prefix_ = prefix_ + '/' + blueprint.prefix_;
            blueprint.static_dir_ = static_dir_ + '/' + blueprint.static_dir_;
            blueprint.templates_dir_ = templates_dir_ + '/' + blueprint.templates_dir_;
            for (auto& rule : blueprint.all_rules_)
            {
                std::string new_rule = '/' + prefix_ + rule->rule_;
                rule->rule_ = new_rule;
            }
            for (Blueprint* bp_child : blueprint.blueprints_)
            {
                Blueprint& bp_ref = *bp_child;
                apply_blueprint(bp_ref);
            }
        }

        std::string prefix_;
        std::string static_dir_;
        std::string templates_dir_;
        std::vector<std::unique_ptr<BaseRule>> all_rules_;
        CatchallRule catchall_rule_;
        std::vector<Blueprint*> blueprints_;

        friend class Router;
    };

    /// Handles matching requests to existing rules and upgrade requests.
    class Router
    {
    public:
        Router()
        {}

        DynamicRule& new_rule_dynamic(const std::string& rule)
        {
            auto ruleObject = new DynamicRule(rule);
            all_rules_.emplace_back(ruleObject);

            return *ruleObject;
        }

        template<uint64_t N>
        typename black_magic::arguments<N>::type::template rebind<TaggedRule>& new_rule_tagged(const std::string& rule)
        {
            using RuleT = typename black_magic::arguments<N>::type::template rebind<TaggedRule>;

            auto ruleObject = new RuleT(rule);
            all_rules_.emplace_back(ruleObject);

            return *ruleObject;
        }

        CatchallRule& catchall_rule()
        {
            return catchall_rule_;
        }

        void internal_add_rule_object(const std::string& rule, BaseRule* ruleObject, const uint16_t& BP_index, std::vector<Blueprint*>& blueprints)
        {
            bool has_trailing_slash = false;
            std::string rule_without_trailing_slash;
            if (rule.size() > 1 && rule.back() == '/')
            {
                has_trailing_slash = true;
                rule_without_trailing_slash = rule;
                rule_without_trailing_slash.pop_back();
            }

            ruleObject->foreach_method([&](int method) {
                per_methods_[method].rules.emplace_back(ruleObject);
                per_methods_[method].trie.add(rule, per_methods_[method].rules.size() - 1, BP_index != INVALID_BP_ID ? blueprints[BP_index]->prefix().length() : 0, BP_index);

                // directory case:
                //   request to '/about' url matches '/about/' rule
                if (has_trailing_slash)
                {
                    per_methods_[method].trie.add(rule_without_trailing_slash, RULE_SPECIAL_REDIRECT_SLASH, BP_index != INVALID_BP_ID ? blueprints_[BP_index]->prefix().length() : 0, BP_index);
                }
            });
        }

        void register_blueprint(Blueprint& blueprint)
        {
            if (std::find(blueprints_.begin(), blueprints_.end(), &blueprint) == blueprints_.end())
            {
                blueprints_.emplace_back(&blueprint);
            }
            else
                throw std::runtime_error("blueprint \"" + blueprint.prefix_ + "\" already exists in router");
        }

        void get_recursive_child_methods(Blueprint* blueprint, std::vector<HTTPMethod>& methods)
        {
            //we only need to deal with children if the blueprint has absolutely no methods (meaning its index won't be added to the trie)
            if (blueprint->static_dir_.empty() && blueprint->all_rules_.empty())
            {
                for (Blueprint* bp : blueprint->blueprints_)
                {
                    get_recursive_child_methods(bp, methods);
                }
            }
            else if (!blueprint->static_dir_.empty())
                methods.emplace_back(HTTPMethod::Get);
            for (auto& rule : blueprint->all_rules_)
            {
                rule->foreach_method([&methods](unsigned method) {
                    HTTPMethod method_final = static_cast<HTTPMethod>(method);
                    if (std::find(methods.begin(), methods.end(), method_final) == methods.end())
                        methods.emplace_back(method_final);
                });
            }
        }

        void validate_bp(std::vector<Blueprint*> blueprints)
        {
            for (unsigned i = 0; i < blueprints.size(); i++)
            {
                Blueprint* blueprint = blueprints[i];
                if (blueprint->static_dir_ == "" && blueprint->all_rules_.empty())
                {
                    std::vector<HTTPMethod> methods;
                    get_recursive_child_methods(blueprint, methods);
                    for (HTTPMethod x : methods)
                    {
                        int i = static_cast<int>(x);
                        per_methods_[i].trie.add(blueprint->prefix(), 0, blueprint->prefix().length(), i);
                    }
                }
                for (auto& rule : blueprint->all_rules_)
                {
                    if (rule)
                    {
                        auto upgraded = rule->upgrade();
                        if (upgraded)
                            rule = std::move(upgraded);
                        rule->validate();
                        internal_add_rule_object(rule->rule(), rule.get(), i, blueprints);
                    }
                }
                validate_bp(blueprint->blueprints_);
            }
        }

        void validate()
        {
            //Take all the routes from the registered blueprints and add them to `all_rules_` to be processed.
            validate_bp(blueprints_);

            for (auto& rule : all_rules_)
            {
                if (rule)
                {
                    auto upgraded = rule->upgrade();
                    if (upgraded)
                        rule = std::move(upgraded);
                    rule->validate();
                    internal_add_rule_object(rule->rule(), rule.get(), INVALID_BP_ID, blueprints_);
                }
            }
            for (auto& per_method : per_methods_)
            {
                per_method.trie.validate();
            }
        }

        // TODO maybe add actual_method
        template<typename Adaptor>
        void handle_upgrade(const request& req, response& res, Adaptor&& adaptor)
        {
            if (req.method >= HTTPMethod::InternalMethodCount)
                return;

            auto& per_method = per_methods_[static_cast<int>(req.method)];
            auto& rules = per_method.rules;
            unsigned rule_index = std::get<0>(per_method.trie.find(req.url));

            if (!rule_index)
            {
                for (auto& per_method : per_methods_)
                {
                    if (std::get<0>(per_method.trie.find(req.url)))
                    {
                        CROW_LOG_DEBUG << "Cannot match method " << req.url << " " << method_name(req.method);
                        res = response(405);
                        res.end();
                        return;
                    }
                }

                CROW_LOG_INFO << "Cannot match rules " << req.url;
                res = response(404);
                res.end();
                return;
            }

            if (rule_index >= rules.size())
                throw std::runtime_error("Trie internal structure corrupted!");

            if (rule_index == RULE_SPECIAL_REDIRECT_SLASH)
            {
                CROW_LOG_INFO << "Redirecting to a url with trailing slash: " << req.url;
                res = response(301);

                // TODO(ipkn) absolute url building
                if (req.get_header_value("Host").empty())
                {
                    res.add_header("Location", req.url + "/");
                }
                else
                {
                    res.add_header("Location", "http://" + req.get_header_value("Host") + req.url + "/");
                }
                res.end();
                return;
            }

            CROW_LOG_DEBUG << "Matched rule (upgrade) '" << rules[rule_index]->rule_ << "' " << static_cast<uint32_t>(req.method) << " / " << rules[rule_index]->get_methods();

            // any uncaught exceptions become 500s
            try
            {
                rules[rule_index]->handle_upgrade(req, res, std::move(adaptor));
            }
            catch (std::exception& e)
            {
                CROW_LOG_ERROR << "An uncaught exception occurred: " << e.what();
                res = response(500);
                res.end();
                return;
            }
            catch (...)
            {
                CROW_LOG_ERROR << "An uncaught exception occurred. The type was unknown so no information was available.";
                res = response(500);
                res.end();
                return;
            }
        }

        void get_found_bp(std::vector<uint16_t>& bp_i, std::vector<Blueprint*>& blueprints, std::vector<Blueprint*>& found_bps, uint16_t index = 0)
        {
            // This statement makes 3 assertions:
            // 1. The index is above 0.
            // 2. The index does not lie outside the given blueprint list.
            // 3. The next blueprint we're adding has a prefix that starts the same as the already added blueprint + a slash (the rest is irrelevant).
            //
            // This is done to prevent a blueprint that has a prefix of "bp_prefix2" to be assumed as a child of one that has "bp_prefix".
            //
            // If any of the assertions is untrue, we delete the last item added, and continue using the blueprint list of the blueprint found before, the topmost being the router's list
            auto verify_prefix = [&bp_i, &index, &blueprints, &found_bps]() {
                return index > 0 &&
                       bp_i[index] < blueprints.size() &&
                       blueprints[bp_i[index]]->prefix().substr(0, found_bps[index - 1]->prefix().length() + 1).compare(std::string(found_bps[index - 1]->prefix() + '/')) == 0;
            };
            if (index < bp_i.size())
            {

                if (verify_prefix())
                {
                    found_bps.push_back(blueprints[bp_i[index]]);
                    get_found_bp(bp_i, found_bps.back()->blueprints_, found_bps, ++index);
                }
                else
                {
                    if (found_bps.size() < 2)
                    {
                        found_bps.clear();
                        found_bps.push_back(blueprints_[bp_i[index]]);
                    }
                    else
                    {
                        found_bps.pop_back();
                        Blueprint* last_element = found_bps.back();
                        found_bps.push_back(last_element->blueprints_[bp_i[index]]);
                    }
                    get_found_bp(bp_i, found_bps.back()->blueprints_, found_bps, ++index);
                }
            }
        }

        /// Is used to handle errors, you insert the error code, found route, request, and response. and it'll either call the appropriate catchall route (considering the blueprint system) and send you a status string (which is mainly used for debug messages), or just set the response code to the proper error code.
        std::string get_error(unsigned short code, std::tuple<uint16_t, std::vector<uint16_t>, routing_params>& found, const request& req, response& res)
        {
            res.code = code;
            std::vector<Blueprint*> bps_found;
            get_found_bp(std::get<1>(found), blueprints_, bps_found);
            for (int i = bps_found.size() - 1; i > 0; i--)
            {
                std::vector<uint16_t> bpi = std::get<1>(found);
                if (bps_found[i]->catchall_rule().has_handler())
                {
                    bps_found[i]->catchall_rule().handler_(req, res);
#ifdef CROW_ENABLE_DEBUG
                    return std::string("Redirected to Blueprint \"" + bps_found[i]->prefix() + "\" Catchall rule");
#else
                    return std::string();
#endif
                }
            }
            if (catchall_rule_.has_handler())
            {
                catchall_rule_.handler_(req, res);
#ifdef CROW_ENABLE_DEBUG
                return std::string("Redirected to global Catchall rule");
#else
                return std::string();
#endif
            }
            return std::string();
        }

        void handle(request& req, response& res)
        {
            HTTPMethod method_actual = req.method;
            if (req.method >= HTTPMethod::InternalMethodCount)
                return;
            else if (req.method == HTTPMethod::Head)
            {
                method_actual = HTTPMethod::Get;
                res.skip_body = true;
            }
            else if (req.method == HTTPMethod::Options)
            {
                std::string allow = "OPTIONS, HEAD, ";

                if (req.url == "/*")
                {
                    for (int i = 0; i < static_cast<int>(HTTPMethod::InternalMethodCount); i++)
                    {
                        if (!per_methods_[i].trie.is_empty())
                        {
                            allow += method_name(static_cast<HTTPMethod>(i)) + ", ";
                        }
                    }
                    allow = allow.substr(0, allow.size() - 2);
                    res = response(204);
                    res.set_header("Allow", allow);
                    res.end();
                    return;
                }
                else
                {
                    for (int i = 0; i < static_cast<int>(HTTPMethod::InternalMethodCount); i++)
                    {
                        if (std::get<0>(per_methods_[i].trie.find(req.url)))
                        {
                            allow += method_name(static_cast<HTTPMethod>(i)) + ", ";
                        }
                    }
                    if (allow != "OPTIONS, HEAD, ")
                    {
                        allow = allow.substr(0, allow.size() - 2);
                        res = response(204);
                        res.set_header("Allow", allow);
                        res.end();
                        return;
                    }
                    else
                    {
                        CROW_LOG_DEBUG << "Cannot match rules " << req.url;
                        res = response(404);
                        res.end();
                        return;
                    }
                }
            }

            auto& per_method = per_methods_[static_cast<int>(method_actual)];
            auto& trie = per_method.trie;
            auto& rules = per_method.rules;

            auto found = trie.find(req.url);

            unsigned rule_index = std::get<0>(found);

            if (!rule_index)
            {
                for (auto& per_method : per_methods_)
                {
                    if (std::get<0>(per_method.trie.find(req.url))) //Route found, but in another method
                    {
                        const std::string error_message(get_error(405, found, req, res));
                        CROW_LOG_DEBUG << "Cannot match method " << req.url << " " << method_name(method_actual) << ". " << error_message;
                        res.end();
                        return;
                    }
                }
                //Route does not exist anywhere

                const std::string error_message(get_error(404, found, req, res));
                CROW_LOG_DEBUG << "Cannot match rules " << req.url << ". " << error_message;
                res.end();
                return;
            }

            if (rule_index >= rules.size())
                throw std::runtime_error("Trie internal structure corrupted!");

            if (rule_index == RULE_SPECIAL_REDIRECT_SLASH)
            {
                CROW_LOG_INFO << "Redirecting to a url with trailing slash: " << req.url;
                res = response(301);

                // TODO(ipkn) absolute url building
                if (req.get_header_value("Host").empty())
                {
                    res.add_header("Location", req.url + "/");
                }
                else
                {
                    res.add_header("Location", "http://" + req.get_header_value("Host") + req.url + "/");
                }
                res.end();
                return;
            }

            CROW_LOG_DEBUG << "Matched rule '" << rules[rule_index]->rule_ << "' " << static_cast<uint32_t>(req.method) << " / " << rules[rule_index]->get_methods();

            // any uncaught exceptions become 500s
            try
            {
                rules[rule_index]->handle(req, res, std::get<2>(found));
            }
            catch (std::exception& e)
            {
                CROW_LOG_ERROR << "An uncaught exception occurred: " << e.what();
                res = response(500);
                res.end();
                return;
            }
            catch (...)
            {
                CROW_LOG_ERROR << "An uncaught exception occurred. The type was unknown so no information was available.";
                res = response(500);
                res.end();
                return;
            }
        }

        void debug_print()
        {
            for (int i = 0; i < static_cast<int>(HTTPMethod::InternalMethodCount); i++)
            {
                CROW_LOG_DEBUG << method_name(static_cast<HTTPMethod>(i));
                per_methods_[i].trie.debug_print();
            }
        }

        std::vector<Blueprint*>& blueprints()
        {
            return blueprints_;
        }

    private:
        CatchallRule catchall_rule_;

        struct PerMethod
        {
            std::vector<BaseRule*> rules;
            Trie trie;

            // rule index 0, 1 has special meaning; preallocate it to avoid duplication.
            PerMethod():
              rules(2) {}
        };
        std::array<PerMethod, static_cast<int>(HTTPMethod::InternalMethodCount)> per_methods_;
        std::vector<std::unique_ptr<BaseRule>> all_rules_;
        std::vector<Blueprint*> blueprints_;
    };
} // namespace crow
