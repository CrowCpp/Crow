/**
 * \file crow/app.h
 * \brief This file includes the definition of the crow::Crow class,
 * the crow::App and crow::SimpleApp aliases, and some macros.
 *
 * In this file are defined:
 * - crow::Crow
 * - crow::App
 * - crow::SimpleApp
 * - \ref CROW_ROUTE
 * - \ref CROW_BP_ROUTE
 * - \ref CROW_WEBSOCKET_ROUTE
 * - \ref CROW_MIDDLEWARES
 * - \ref CROW_CATCHALL_ROUTE
 * - \ref CROW_BP_CATCHALL_ROUTE
 */

#pragma once

#include <chrono>
#include <string>
#include <functional>
#include <memory>
#include <future>
#include <cstdint>
#include <type_traits>
#include <thread>
#include <condition_variable>

#include "crow/version.h"
#include "crow/settings.h"
#include "crow/logging.h"
#include "crow/utility.h"
#include "crow/routing.h"
#include "crow/middleware_context.h"
#include "crow/http_request.h"
#include "crow/http_server.h"
#include "crow/task_timer.h"
#include "crow/websocket.h"
#ifdef CROW_ENABLE_COMPRESSION
#include "crow/compression.h"
#endif // #ifdef CROW_ENABLE_COMPRESSION


#ifdef CROW_MSVC_WORKAROUND

#define CROW_ROUTE(app, url) app.route_dynamic(url) // See the documentation in the comment below.
#define CROW_BP_ROUTE(blueprint, url) blueprint.new_rule_dynamic(url) // See the documentation in the comment below.

#else // #ifdef CROW_MSVC_WORKAROUND

/**
 * \def CROW_ROUTE(app, url)
 * \brief Creates a route for app using a rule.
 *
 * It use crow::Crow::route_dynamic or crow::Crow::route to define
 * a rule for your application. It's usage is like this:
 *
 * ```cpp
 * auto app = crow::SimpleApp(); // or crow::App()
 * CROW_ROUTE(app, "/")
 * ([](){
 *     return "<h1>Hello, world!</h1>";
 * });
 * ```
 *
 * This is the recommended way to define routes in a crow application.
 * \see [Page of guide "Routes"](https://crowcpp.org/master/guides/routes/).
 */
#define CROW_ROUTE(app, url) app.template route<crow::black_magic::get_parameter_tag(url)>(url)

/**
 * \def CROW_BP_ROUTE(blueprint, url)
 * \brief Creates a route for a blueprint using a rule.
 *
 * It may use crow::Blueprint::new_rule_dynamic or
 * crow::Blueprint::new_rule_tagged to define a new rule for
 * an given blueprint. It's usage is similar
 * to CROW_ROUTE macro:
 *
 * ```cpp
 * crow::Blueprint my_bp();
 * CROW_BP_ROUTE(my_bp, "/")
 * ([](){
 *     return "<h1>Hello, world!</h1>";
 * });
 * ```
 *
 * This is the recommended way to define routes in a crow blueprint
 * because of its compile-time capabilities.
 *
 * \see [Page of the guide "Blueprints"](https://crowcpp.org/master/guides/blueprints/).
 */
#define CROW_BP_ROUTE(blueprint, url) blueprint.new_rule_tagged<crow::black_magic::get_parameter_tag(url)>(url)

/**
 * \def CROW_WEBSOCKET_ROUTE(app, url)
 * \brief Defines WebSocket route for app.
 *
 * It binds a WebSocket route to app. Easy solution to implement
 * WebSockets in your app. The usage syntax of this macro is
 * like this:
 *
 * ```cpp
 * auto app = crow::SimpleApp(); // or crow::App()
 * CROW_WEBSOCKET_ROUTE(app, "/ws")
 *     .onopen([&](crow::websocket::connection& conn){
 *                do_something();
 *            })
 *     .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t){
 *                 do_something();
 *             })
 *     .onmessage([&](crow::websocket::connection&, const std::string& data, bool is_binary){
 *                   if (is_binary)
 *                       do_something(data);
 *                   else
 *                       do_something_else(data);
 *               });
 * ```
 *
 * \see [Page of the guide "WebSockets"](https://crowcpp.org/master/guides/websockets/).
 */
#define CROW_WEBSOCKET_ROUTE(app, url) app.route<crow::black_magic::get_parameter_tag(url)>(url).websocket<std::remove_reference<decltype(app)>::type>(&app)

/**
 * \def CROW_MIDDLEWARES(app, ...)
 * \brief Enable a Middleware for an specific route in app
 * or blueprint.
 *
 * It defines the usage of a Middleware in one route. And it
 * can be used in both crow::SimpleApp (and crow::App) instances and
 * crow::Blueprint. Its usage syntax is like this:
 *
 * ```cpp
 * auto app = crow::SimpleApp(); // or crow::App()
 * CROW_ROUTE(app, "/with_middleware")
 * .CROW_MIDDLEWARES(app, LocalMiddleware) // Can be used more than one
 * ([]() {                                 // middleware.
 *     return "Hello world!";
 * });
 * ```
 *
 * \see [Page of the guide "Middlewares"](https://crowcpp.org/master/guides/middleware/).
 */
#define CROW_MIDDLEWARES(app, ...) template middlewares<typename std::remove_reference<decltype(app)>::type, __VA_ARGS__>()

#endif // #ifdef CROW_MSVC_WORKAROUND

/**
 * \def CROW_CATCHALL_ROUTE(app)
 * \brief Defines a custom catchall route for app using a
 * custom rule.
 *
 * It defines a handler when the client make a request for an
 * undefined route. Instead of just reply with a `404` status
 * code (default behavior), you can define a custom handler
 * using this macro.
 *
 * \see [Page of the guide "Routes" (Catchall routes)](https://crowcpp.org/master/guides/routes/#catchall-routes).
 */
#define CROW_CATCHALL_ROUTE(app) app.catchall_route()

/**
 * \def CROW_BP_CATCHALL_ROUTE(blueprint)
 * \brief Defines a custom catchall route for blueprint
 * using a custom rule.
 *
 * It defines a handler when the client make a request for an
 * undefined route in the blueprint.
 *
 * \see [Page of the guide "Blueprint" (Define a custom Catchall route)](https://crowcpp.org/master/guides/blueprints/#define-a-custom-catchall-route).
 */
#define CROW_BP_CATCHALL_ROUTE(blueprint) blueprint.catchall_rule()


/**
 * \namespace crow
 * \brief The main namespace of the library. In this namespace
 * is defined the most important classes and functions of the
 * library.
 *
 * Within this namespace, the Crow class, Router class, Connection
 * class, and other are defined.
 */
namespace crow
{
#ifdef CROW_ENABLE_SSL
    using ssl_context_t = asio::ssl::context;
#endif
    /**
     * \class Crow
     * \brief The main server application class.
     *
     * Use crow::SimpleApp or crow::App<Middleware1, Middleware2, etc...> instead of
     * directly instantiate this class.
     */
    template<typename... Middlewares>
    class Crow
    {
    public:
        /// \brief This is the crow application
        using self_t = Crow;

        /// \brief The HTTP server
        using server_t = Server<Crow, SocketAdaptor, Middlewares...>;

#ifdef CROW_ENABLE_SSL
        /// \brief An HTTP server that runs on SSL with an SSLAdaptor
        using ssl_server_t = Server<Crow, SSLAdaptor, Middlewares...>;
#endif
        Crow()
        {}

        /// \brief Construct Crow with a subset of middleware
        template<typename... Ts>
        Crow(Ts&&... ts):
          middlewares_(make_middleware_tuple(std::forward<Ts>(ts)...))
        {}

        /// \brief Process an Upgrade request
        ///
        /// Currently used to upgrade an HTTP connection to a WebSocket connection
        template<typename Adaptor>
        void handle_upgrade(const request& req, response& res, Adaptor&& adaptor)
        {
            router_.handle_upgrade(req, res, adaptor);
        }

        /// \brief Process only the method and URL of a request and provide a route (or an error response)
        std::unique_ptr<routing_handle_result> handle_initial(request& req, response& res)
        {
            return router_.handle_initial(req, res);
        }

        /// \brief Process the fully parsed request and generate a response for it
        void handle(request& req, response& res, std::unique_ptr<routing_handle_result>& found)
        {
            router_.handle<self_t>(req, res, *found);
        }

        /// \brief Process a fully parsed request from start to finish (primarily used for debugging)
        void handle_full(request& req, response& res)
        {
            auto found = handle_initial(req, res);
            if (found->rule_index)
                handle(req, res, found);
        }

        /// \brief Create a dynamic route using a rule (**Use CROW_ROUTE instead**)
        DynamicRule& route_dynamic(const std::string& rule)
        {
            return router_.new_rule_dynamic(rule);
        }

        /// \brief Create a route using a rule (**Use CROW_ROUTE instead**)
        template<uint64_t Tag>
        auto route(const std::string& rule)
          -> typename std::invoke_result<decltype(&Router::new_rule_tagged<Tag>), Router, const std::string&>::type
        {
            return router_.new_rule_tagged<Tag>(rule);
        }

        /// \brief Create a route for any requests without a proper route (**Use CROW_CATCHALL_ROUTE instead**)
        CatchallRule& catchall_route()
        {
            return router_.catchall_rule();
        }

        /// \brief Set the default max payload size for websockets
        self_t& websocket_max_payload(uint64_t max_payload)
        {
            max_payload_ = max_payload;
            return *this;
        }

        /// \brief Get the default max payload size for websockets
        uint64_t websocket_max_payload()
        {
            return max_payload_;
        }

        self_t& signal_clear()
        {
            signals_.clear();
            return *this;
        }

        self_t& signal_add(int signal_number)
        {
            signals_.push_back(signal_number);
            return *this;
        }

        std::vector<int> signals()
        {
            return signals_;
        }

        /// \brief Set the port that Crow will handle requests on
        self_t& port(std::uint16_t port)
        {
            port_ = port;
            return *this;
        }

        /// \brief Get the port that Crow will handle requests on
        std::uint16_t port() const
        {
            if (!server_started_)
            {
                return port_;
            }
#ifdef CROW_ENABLE_SSL
            if (ssl_used_)
            {
                return ssl_server_->port();
            }
            else
#endif
            {
                return server_->port();
            }
        }

        /// \brief Set the connection timeout in seconds (default is 5)
        self_t& timeout(std::uint8_t timeout)
        {
            timeout_ = timeout;
            return *this;
        }

        /// \brief Set the server name
        self_t& server_name(std::string server_name)
        {
            server_name_ = server_name;
            return *this;
        }

        /// \brief The IP address that Crow will handle requests on (default is 0.0.0.0)
        self_t& bindaddr(std::string bindaddr)
        {
            bindaddr_ = bindaddr;
            return *this;
        }

        /// \brief Get the address that Crow will handle requests on
        std::string bindaddr()
        {
            return bindaddr_;
        }

        /// \brief Run the server on multiple threads using all available threads
        self_t& multithreaded()
        {
            return concurrency(std::thread::hardware_concurrency());
        }

        /// \brief Run the server on multiple threads using a specific number
        self_t& concurrency(std::uint16_t concurrency)
        {
            if (concurrency < 2) // Crow can have a minimum of 2 threads running
                concurrency = 2;
            concurrency_ = concurrency;
            return *this;
        }

        /// \brief Get the number of threads that server is using
        std::uint16_t concurrency() const
        {
            return concurrency_;
        }

        /// \brief Set the server's log level
        ///
        /// Possible values are:
        /// - crow::LogLevel::Debug       (0)
        /// - crow::LogLevel::Info        (1)
        /// - crow::LogLevel::Warning     (2)
        /// - crow::LogLevel::Error       (3)
        /// - crow::LogLevel::Critical    (4)
        self_t& loglevel(LogLevel level)
        {
            crow::logger::setLogLevel(level);
            return *this;
        }

        /// \brief Set the response body size (in bytes) beyond which Crow automatically streams responses (Default is 1MiB)
        ///
        /// Any streamed response is unaffected by Crow's timer, and therefore won't timeout before a response is fully sent.
        self_t& stream_threshold(size_t threshold)
        {
            res_stream_threshold_ = threshold;
            return *this;
        }

        /// \brief Get the response body size (in bytes) beyond which Crow automatically streams responses
        size_t& stream_threshold()
        {
            return res_stream_threshold_;
        }


        self_t& register_blueprint(Blueprint& blueprint)
        {
            router_.register_blueprint(blueprint);
            return *this;
        }

        /// \brief Set the function to call to handle uncaught exceptions generated in routes (Default generates error 500).
        ///
        /// The function must have the following signature: void(crow::response&).
        /// It must set the response passed in argument to the function, which will be sent back to the client.
        /// See Router::default_exception_handler() for the default implementation.
        template<typename Func>
        self_t& exception_handler(Func&& f)
        {
            router_.exception_handler() = std::forward<Func>(f);
            return *this;
        }

        std::function<void(crow::response&)>& exception_handler()
        {
            return router_.exception_handler();
        }

        /// \brief Set a custom duration and function to run on every tick
        template<typename Duration, typename Func>
        self_t& tick(Duration d, Func f)
        {
            tick_interval_ = std::chrono::duration_cast<std::chrono::milliseconds>(d);
            tick_function_ = f;
            return *this;
        }

#ifdef CROW_ENABLE_COMPRESSION

        self_t& use_compression(compression::algorithm algorithm)
        {
            comp_algorithm_ = algorithm;
            compression_used_ = true;
            return *this;
        }

        compression::algorithm compression_algorithm()
        {
            return comp_algorithm_;
        }

        bool compression_used() const
        {
            return compression_used_;
        }
#endif

        /// \brief Apply blueprints
        void add_blueprint()
        {
#if defined(__APPLE__) || defined(__MACH__)
            if (router_.blueprints().empty()) return;
#endif

            for (Blueprint* bp : router_.blueprints())
            {
                if (bp->static_dir().empty()) {
                    CROW_LOG_ERROR << "Blueprint " << bp->prefix() << " and its sub-blueprints ignored due to empty static directory.";
                    continue;
                }
                auto static_dir_ = crow::utility::normalize_path(bp->static_dir());

                bp->new_rule_tagged<crow::black_magic::get_parameter_tag(CROW_STATIC_ENDPOINT)>(CROW_STATIC_ENDPOINT)([static_dir_](crow::response& res, std::string file_path_partial) {
                    utility::sanitize_filename(file_path_partial);
                    res.set_static_file_info_unsafe(static_dir_ + file_path_partial);
                    res.end();
                });
            }

            router_.validate_bp();
        }

        /// \brief Go through the rules, upgrade them if possible, and add them to the list of rules
        void add_static_dir()
        {
            if (are_static_routes_added()) return;
            auto static_dir_ = crow::utility::normalize_path(CROW_STATIC_DIRECTORY);

            route<crow::black_magic::get_parameter_tag(CROW_STATIC_ENDPOINT)>(CROW_STATIC_ENDPOINT)([static_dir_](crow::response& res, std::string file_path_partial) {
                utility::sanitize_filename(file_path_partial);
                res.set_static_file_info_unsafe(static_dir_ + file_path_partial);
                res.end();
            });
            set_static_routes_added();
        }

        /// \brief A wrapper for `validate()` in the router
        void validate()
        {
            router_.validate();
        }

        /// \brief Run the server
        void run()
        {
#ifndef CROW_DISABLE_STATIC_DIR
            add_blueprint();
            add_static_dir();
#endif
            validate();

            error_code ec;
            asio::ip::address addr = asio::ip::make_address(bindaddr_,ec);
            if (ec){
                CROW_LOG_ERROR << ec.message() << " - Can not create valid ip address from string: \"" << bindaddr_ << "\"";
                return;
            }
            tcp::endpoint endpoint(addr, port_);
#ifdef CROW_ENABLE_SSL
            if (ssl_used_)
            {
                router_.using_ssl = true;
                ssl_server_ = std::move(std::unique_ptr<ssl_server_t>(new ssl_server_t(this, endpoint, server_name_, &middlewares_, concurrency_, timeout_, &ssl_context_)));
                ssl_server_->set_tick_function(tick_interval_, tick_function_);
                ssl_server_->signal_clear();
                for (auto snum : signals_)
                {
                    ssl_server_->signal_add(snum);
                }
                notify_server_start();
                ssl_server_->run();
            }
            else
#endif
            {
                server_ = std::move(std::unique_ptr<server_t>(new server_t(this, endpoint, server_name_, &middlewares_, concurrency_, timeout_, nullptr)));
                server_->set_tick_function(tick_interval_, tick_function_);
                for (auto snum : signals_)
                {
                    server_->signal_add(snum);
                }
                notify_server_start();
                server_->run();
            }
        }

        /// \brief Non-blocking version of \ref run()
        ///
        /// The output from this method needs to be saved into a variable!
        /// Otherwise the call will be made on the same thread.
        std::future<void> run_async()
        {
            return std::async(std::launch::async, [&] {
                this->run();
            });
        }

        /// \brief Stop the server
        void stop()
        {
#ifdef CROW_ENABLE_SSL
            if (ssl_used_)
            {
                if (ssl_server_) { ssl_server_->stop(); }
            }
            else
#endif
            {
                // TODO(EDev): Move these 6 lines to a method in http_server.
                std::vector<crow::websocket::connection*> websockets_to_close = websockets_;
                for (auto websocket : websockets_to_close)
                {
                    CROW_LOG_INFO << "Quitting Websocket: " << websocket;
                    websocket->close("Server Application Terminated");
                }
                if (server_) { server_->stop(); }
            }
        }

        void add_websocket(crow::websocket::connection* conn)
        {
            websockets_.push_back(conn);
        }

        void remove_websocket(crow::websocket::connection* conn)
        {
            websockets_.erase(std::remove(websockets_.begin(), websockets_.end(), conn), websockets_.end());
        }

        /// \brief Print the routing paths defined for each HTTP method
        void debug_print()
        {
            CROW_LOG_DEBUG << "Routing:";
            router_.debug_print();
        }


#ifdef CROW_ENABLE_SSL

        /// \brief Use certificate and key files for SSL
        self_t& ssl_file(const std::string& crt_filename, const std::string& key_filename)
        {
            ssl_used_ = true;
            ssl_context_.set_verify_mode(asio::ssl::verify_peer);
            ssl_context_.set_verify_mode(asio::ssl::verify_client_once);
            ssl_context_.use_certificate_file(crt_filename, ssl_context_t::pem);
            ssl_context_.use_private_key_file(key_filename, ssl_context_t::pem);
            ssl_context_.set_options(
              asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
            return *this;
        }

        /// \brief Use `.pem` file for SSL
        self_t& ssl_file(const std::string& pem_filename)
        {
            ssl_used_ = true;
            ssl_context_.set_verify_mode(asio::ssl::verify_peer);
            ssl_context_.set_verify_mode(asio::ssl::verify_client_once);
            ssl_context_.load_verify_file(pem_filename);
            ssl_context_.set_options(
              asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
            return *this;
        }

        /// \brief Use certificate chain and key files for SSL
        self_t& ssl_chainfile(const std::string& crt_filename, const std::string& key_filename)
        {
            ssl_used_ = true;
            ssl_context_.set_verify_mode(asio::ssl::verify_peer);
            ssl_context_.set_verify_mode(asio::ssl::verify_client_once);
            ssl_context_.use_certificate_chain_file(crt_filename);
            ssl_context_.use_private_key_file(key_filename, ssl_context_t::pem);
            ssl_context_.set_options(
              asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3);
            return *this;
        }

        self_t& ssl(asio::ssl::context&& ctx)
        {
            ssl_used_ = true;
            ssl_context_ = std::move(ctx);
            return *this;
        }

        bool ssl_used() const
        {
            return ssl_used_;
        }
#else

        template<typename T, typename... Remain>
        self_t& ssl_file(T&&, Remain&&...)
        {
            // We can't call .ssl() member function unless CROW_ENABLE_SSL is defined.
            static_assert(
              // make static_assert dependent to T; always false
              std::is_base_of<T, void>::value,
              "Define CROW_ENABLE_SSL to enable ssl support.");
            return *this;
        }

        template<typename T, typename... Remain>
        self_t& ssl_chainfile(T&&, Remain&&...)
        {
            // We can't call .ssl() member function unless CROW_ENABLE_SSL is defined.
            static_assert(
              // make static_assert dependent to T; always false
              std::is_base_of<T, void>::value,
              "Define CROW_ENABLE_SSL to enable ssl support.");
            return *this;
        }

        template<typename T>
        self_t& ssl(T&&)
        {
            // We can't call .ssl() member function unless CROW_ENABLE_SSL is defined.
            static_assert(
              // make static_assert dependent to T; always false
              std::is_base_of<T, void>::value,
              "Define CROW_ENABLE_SSL to enable ssl support.");
            return *this;
        }

        bool ssl_used() const
        {
            return false;
        }
#endif

        // middleware
        using context_t = detail::context<Middlewares...>;
        using mw_container_t = std::tuple<Middlewares...>;
        template<typename T>
        typename T::context& get_context(const request& req)
        {
            static_assert(black_magic::contains<T, Middlewares...>::value, "App doesn't have the specified middleware type.");
            auto& ctx = *reinterpret_cast<context_t*>(req.middleware_context);
            return ctx.template get<T>();
        }

        template<typename T>
        T& get_middleware()
        {
            return utility::get_element_by_type<T, Middlewares...>(middlewares_);
        }

        /// \brief Wait until the server has properly started
        std::cv_status wait_for_server_start(std::chrono::milliseconds wait_timeout = std::chrono::milliseconds(3000))
        {
            std::cv_status status = std::cv_status::no_timeout;
            auto wait_until = std::chrono::steady_clock::now() + wait_timeout;
            {
                std::unique_lock<std::mutex> lock(start_mutex_);
                while (!server_started_ && (status == std::cv_status::no_timeout))
                {
                    status = cv_started_.wait_until(lock, wait_until);
                }
            }
            
            if (status == std::cv_status::no_timeout)
            {
                if (server_)
                {
                    status = server_->wait_for_start(wait_until);
                }
#ifdef CROW_ENABLE_SSL
                else if (ssl_server_)
                {
                    status = ssl_server_->wait_for_start(wait_until);
                }
#endif
            }
            return status;
        }

    private:
        template<typename... Ts>
        std::tuple<Middlewares...> make_middleware_tuple(Ts&&... ts)
        {
            auto fwd = std::forward_as_tuple((ts)...);
            return std::make_tuple(
              std::forward<Middlewares>(
                black_magic::tuple_extract<Middlewares, decltype(fwd)>(fwd))...);
        }

        /// \brief Notify anything using \ref wait_for_server_start() to proceed
        void notify_server_start()
        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            server_started_ = true;
            cv_started_.notify_all();
        }

        void set_static_routes_added() {
            static_routes_added_ = true;
        }

        bool are_static_routes_added() {
            return static_routes_added_;
        }

    private:
        std::uint8_t timeout_{5};
        uint16_t port_ = 80;
        uint16_t concurrency_ = 2;
        uint64_t max_payload_{UINT64_MAX};
        std::string server_name_ = std::string("Crow/") + VERSION;
        std::string bindaddr_ = "0.0.0.0";
        size_t res_stream_threshold_ = 1048576;
        Router router_;
        bool static_routes_added_{false};

#ifdef CROW_ENABLE_COMPRESSION
        compression::algorithm comp_algorithm_;
        bool compression_used_{false};
#endif

        std::chrono::milliseconds tick_interval_;
        std::function<void()> tick_function_;

        std::tuple<Middlewares...> middlewares_;

#ifdef CROW_ENABLE_SSL
        std::unique_ptr<ssl_server_t> ssl_server_;
        bool ssl_used_{false};
        ssl_context_t ssl_context_{asio::ssl::context::sslv23};
#endif

        std::unique_ptr<server_t> server_;

        std::vector<int> signals_{SIGINT, SIGTERM};

        bool server_started_{false};
        std::condition_variable cv_started_;
        std::mutex start_mutex_;
        std::vector<crow::websocket::connection*> websockets_;
    };

    /// \brief Alias of Crow<Middlewares...>. Useful if you want
    /// a instance of an Crow application that require Middlewares
    template<typename... Middlewares>
    using App = Crow<Middlewares...>;

    /// \brief Alias of Crow<>. Useful if you want a instance of
    /// an Crow application that doesn't require of Middlewares
    using SimpleApp = Crow<>;
} // namespace crow
