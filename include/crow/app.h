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
#endif

#ifdef CROW_MSVC_WORKAROUND
#define CROW_ROUTE(app, url) app.route_dynamic(url)
#define CROW_BP_ROUTE(blueprint, url) blueprint.new_rule_dynamic(url)
#else
#define CROW_ROUTE(app, url) app.template route<crow::black_magic::get_parameter_tag(url)>(url)
#define CROW_BP_ROUTE(blueprint, url) blueprint.new_rule_tagged<crow::black_magic::get_parameter_tag(url)>(url)
#define CROW_WEBSOCKET_ROUTE(app, url) app.route<crow::black_magic::get_parameter_tag(url)>(url).websocket<std::remove_reference<decltype(app)>::type>(&app)
#define CROW_MIDDLEWARES(app, ...) template middlewares<typename std::remove_reference<decltype(app)>::type, __VA_ARGS__>()
#endif
#define CROW_CATCHALL_ROUTE(app) app.catchall_route()
#define CROW_BP_CATCHALL_ROUTE(blueprint) blueprint.catchall_rule()

namespace crow
{
#ifdef CROW_ENABLE_SSL
    using ssl_context_t = asio::ssl::context;
#endif
    /// The main server application

    ///
    /// Use `SimpleApp` or `App<Middleware1, Middleware2, etc...>`
    template<typename... Middlewares>
    class Crow
    {
    public:
        /// This crow application
        using self_t = Crow;
        /// The HTTP server
        using server_t = Server<Crow, SocketAdaptor, Middlewares...>;
#ifdef CROW_ENABLE_SSL
        /// An HTTP server that runs on SSL with an SSLAdaptor
        using ssl_server_t = Server<Crow, SSLAdaptor, Middlewares...>;
#endif
        Crow()
        {}

        /// Construct Crow with a subset of middleware
        template<typename... Ts>
        Crow(Ts&&... ts):
          middlewares_(make_middleware_tuple(std::forward<Ts>(ts)...))
        {}

        /// Process an Upgrade request

        ///
        /// Currently used to upgrade an HTTP connection to a WebSocket connection
        template<typename Adaptor>
        void handle_upgrade(const request& req, response& res, Adaptor&& adaptor)
        {
            router_.handle_upgrade(req, res, adaptor);
        }

        /// Process only the method and URL of a request and provide a route (or an error response)
        std::unique_ptr<routing_handle_result> handle_initial(request& req, response& res)
        {
            return router_.handle_initial(req, res);
        }

        /// Process the fully parsed request and generate a response for it
        void handle(request& req, response& res, std::unique_ptr<routing_handle_result>& found)
        {
            router_.handle<self_t>(req, res, *found);
        }

        /// Process a fully parsed request from start to finish (primarily used for debugging)
        void handle_full(request& req, response& res)
        {
            auto found = handle_initial(req, res);
            if (found->rule_index)
                handle(req, res, found);
        }

        /// Create a dynamic route using a rule (**Use CROW_ROUTE instead**)
        DynamicRule& route_dynamic(std::string&& rule)
        {
            return router_.new_rule_dynamic(std::move(rule));
        }

        ///Create a route using a rule (**Use CROW_ROUTE instead**)
        template<uint64_t Tag>
#ifdef CROW_GCC83_WORKAROUND
        auto& route(std::string&& rule)
#else
        auto route(std::string&& rule)
#endif
#if defined CROW_CAN_USE_CPP17 && !defined CROW_GCC83_WORKAROUND
          -> typename std::invoke_result<decltype(&Router::new_rule_tagged<Tag>), Router, std::string&&>::type
#elif !defined CROW_GCC83_WORKAROUND
          -> typename std::result_of<decltype (&Router::new_rule_tagged<Tag>)(Router, std::string&&)>::type
#endif
        {
            return router_.new_rule_tagged<Tag>(std::move(rule));
        }

        /// Create a route for any requests without a proper route (**Use CROW_CATCHALL_ROUTE instead**)
        CatchallRule& catchall_route()
        {
            return router_.catchall_rule();
        }

        /// Set the default max payload size for websockets
        self_t& websocket_max_payload(uint64_t max_payload)
        {
            max_payload_ = max_payload;
            return *this;
        }

        /// Get the default max payload size for websockets
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

        /// Set the port that Crow will handle requests on
        self_t& port(std::uint16_t port)
        {
            port_ = port;
            return *this;
        }

        /// Get the port that Crow will handle requests on
        std::uint16_t port()
        {
            return port_;
        }

        /// Set the connection timeout in seconds (default is 5)
        self_t& timeout(std::uint8_t timeout)
        {
            timeout_ = timeout;
            return *this;
        }

        /// Set the server name
        self_t& server_name(std::string server_name)
        {
            server_name_ = server_name;
            return *this;
        }

        /// The IP address that Crow will handle requests on (default is 0.0.0.0)
        self_t& bindaddr(std::string bindaddr)
        {
            bindaddr_ = bindaddr;
            return *this;
        }
        
        /// Get the address that Crow will handle requests on
        std::string bindaddr()
        {
            return bindaddr_;
        }

        /// Run the server on multiple threads using all available threads
        self_t& multithreaded()
        {
            return concurrency(std::thread::hardware_concurrency());
        }

        /// Run the server on multiple threads using a specific number
        self_t& concurrency(std::uint16_t concurrency)
        {
            if (concurrency < 2) // Crow can have a minimum of 2 threads running
                concurrency = 2;
            concurrency_ = concurrency;
            return *this;
        }
        
        /// Get the number of threads that server is using
        std::uint16_t concurrency()
        {
            return concurrency_;
        }

        /// Set the server's log level

        ///
        /// Possible values are:<br>
        /// crow::LogLevel::Debug       (0)<br>
        /// crow::LogLevel::Info        (1)<br>
        /// crow::LogLevel::Warning     (2)<br>
        /// crow::LogLevel::Error       (3)<br>
        /// crow::LogLevel::Critical    (4)<br>
        self_t& loglevel(LogLevel level)
        {
            crow::logger::setLogLevel(level);
            return *this;
        }

        /// Set the response body size (in bytes) beyond which Crow automatically streams responses (Default is 1MiB)

        ///
        /// Any streamed response is unaffected by Crow's timer, and therefore won't timeout before a response is fully sent.
        self_t& stream_threshold(size_t threshold)
        {
            res_stream_threshold_ = threshold;
            return *this;
        }

        /// Get the response body size (in bytes) beyond which Crow automatically streams responses
        size_t& stream_threshold()
        {
            return res_stream_threshold_;
        }

        self_t& register_blueprint(Blueprint& blueprint)
        {
            router_.register_blueprint(blueprint);
            return *this;
        }

        /// Set a custom duration and function to run on every tick
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
        /// A wrapper for `validate()` in the router

        ///
        /// Go through the rules, upgrade them if possible, and add them to the list of rules
        void validate()
        {
            if (!validated_)
            {

#ifndef CROW_DISABLE_STATIC_DIR

                // stat on windows doesn't care whether '/' or '\' is being used. on Linux however, using '\' doesn't work. therefore every instance of '\' gets replaced with '/' then a check is done to make sure the directory ends with '/'.
                std::string static_dir_(CROW_STATIC_DIRECTORY);
                std::replace(static_dir_.begin(), static_dir_.end(), '\\', '/');
                if (static_dir_[static_dir_.length() - 1] != '/')
                    static_dir_ += '/';

                route<crow::black_magic::get_parameter_tag(CROW_STATIC_ENDPOINT)>(CROW_STATIC_ENDPOINT)([static_dir_](crow::response& res, std::string file_path_partial) {
                    utility::sanitize_filename(file_path_partial);
                    res.set_static_file_info_unsafe(static_dir_ + file_path_partial);
                    res.end();
                });

#if defined(__APPLE__) || defined(__MACH__)
                if (!router_.blueprints().empty())
#endif
                {
                    for (Blueprint* bp : router_.blueprints())
                    {
                        if (!bp->static_dir().empty())
                        {
                            // stat on windows doesn't care whether '/' or '\' is being used. on Linux however, using '\' doesn't work. therefore every instance of '\' gets replaced with '/' then a check is done to make sure the directory ends with '/'.
                            std::string static_dir_(bp->static_dir());
                            std::replace(static_dir_.begin(), static_dir_.end(), '\\', '/');
                            if (static_dir_[static_dir_.length() - 1] != '/')
                                static_dir_ += '/';

                            bp->new_rule_tagged<crow::black_magic::get_parameter_tag(CROW_STATIC_ENDPOINT)>(CROW_STATIC_ENDPOINT)([static_dir_](crow::response& res, std::string file_path_partial) {
                                utility::sanitize_filename(file_path_partial);
                                res.set_static_file_info_unsafe(static_dir_ + file_path_partial);
                                res.end();
                            });
                        }
                    }
                }
#endif

                router_.validate();
                validated_ = true;
            }
        }

        /// Run the server
        void run()
        {

            validate();

#ifdef CROW_ENABLE_SSL
            if (ssl_used_)
            {
                ssl_server_ = std::move(std::unique_ptr<ssl_server_t>(new ssl_server_t(this, bindaddr_, port_, server_name_, &middlewares_, concurrency_, timeout_, &ssl_context_)));
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
                server_ = std::move(std::unique_ptr<server_t>(new server_t(this, bindaddr_, port_, server_name_, &middlewares_, concurrency_, timeout_, nullptr)));
                server_->set_tick_function(tick_interval_, tick_function_);
                for (auto snum : signals_)
                {
                    server_->signal_add(snum);
                }
                notify_server_start();
                server_->run();
            }
        }

        /// Non-blocking version of \ref run()
        ///
        /// The output from this method needs to be saved into a variable!
        /// Otherwise the call will be made on the same thread.
        std::future<void> run_async()
        {
            return std::async(std::launch::async, [&] {
                this->run();
            });
        }

        /// Stop the server
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

        /// Print the routing paths defined for each HTTP method
        void debug_print()
        {
            CROW_LOG_DEBUG << "Routing:";
            router_.debug_print();
        }


#ifdef CROW_ENABLE_SSL

        /// Use certificate and key files for SSL
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

        /// Use .pem file for SSL
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

        /// Use certificate chain and key files for SSL
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

        /// Wait until the server has properly started
        void wait_for_server_start()
        {
            {
                std::unique_lock<std::mutex> lock(start_mutex_);
                if (!server_started_)
                    cv_started_.wait(lock);
            }
            if (server_)
                server_->wait_for_start();
#ifdef CROW_ENABLE_SSL
            else if (ssl_server_)
                ssl_server_->wait_for_start();
#endif
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

        /// Notify anything using `wait_for_server_start()` to proceed
        void notify_server_start()
        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            server_started_ = true;
            cv_started_.notify_all();
        }


    private:
        std::uint8_t timeout_{5};
        uint16_t port_ = 80;
        uint16_t concurrency_ = 2;
        uint64_t max_payload_{UINT64_MAX};
        bool validated_ = false;
        std::string server_name_ = std::string("Crow/") + VERSION;
        std::string bindaddr_ = "0.0.0.0";
        size_t res_stream_threshold_ = 1048576;
        Router router_;

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
    template<typename... Middlewares>
    using App = Crow<Middlewares...>;
    using SimpleApp = Crow<>;
} // namespace crow
