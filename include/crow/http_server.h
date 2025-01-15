#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#ifdef CROW_ENABLE_SSL
#include <boost/asio/ssl.hpp>
#endif
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#ifdef CROW_ENABLE_SSL
#include <asio/ssl.hpp>
#endif
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

#include "crow/version.h"
#include "crow/http_connection.h"
#include "crow/logging.h"
#include "crow/task_timer.h"


namespace crow // NOTE: Already documented in "crow/app.h"
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
#else
    using error_code = asio::error_code;
#endif
    using tcp = asio::ip::tcp;

    template<typename Handler, typename Adaptor = SocketAdaptor, typename... Middlewares>
    class Server
    {
    public:
      Server(Handler* handler,
             const tcp::endpoint& endpoint,
             std::string server_name = std::string("Crow/") + VERSION,
             std::tuple<Middlewares...>* middlewares = nullptr,
             uint16_t concurrency = 1,
             uint8_t timeout = 5,
             typename Adaptor::context* adaptor_ctx = nullptr):
          acceptor_(io_context_,endpoint),
          signals_(io_context_),
          tick_timer_(io_context_),
          handler_(handler),
          concurrency_(concurrency),
          timeout_(timeout),
          server_name_(server_name),
          task_queue_length_pool_(concurrency_ - 1),
          middlewares_(middlewares),
          adaptor_ctx_(adaptor_ctx)
        {}

        void set_tick_function(std::chrono::milliseconds d, std::function<void()> f)
        {
            tick_interval_ = d;
            tick_function_ = f;
        }

        void on_tick()
        {
            tick_function_();
            tick_timer_.expires_after(std::chrono::milliseconds(tick_interval_.count()));
            tick_timer_.async_wait([this](const error_code& ec) {
                if (ec)
                    return;
                on_tick();
            });
        }

        void run()
        {
            uint16_t worker_thread_count = concurrency_ - 1;
            for (int i = 0; i < worker_thread_count; i++)
                io_context_pool_.emplace_back(new asio::io_context());
            get_cached_date_str_pool_.resize(worker_thread_count);
            task_timer_pool_.resize(worker_thread_count);

            std::vector<std::future<void>> v;
            std::atomic<int> init_count(0);
            for (uint16_t i = 0; i < worker_thread_count; i++)
                v.push_back(
                  std::async(
                    std::launch::async, [this, i, &init_count] {
                        // thread local date string get function
                        auto last = std::chrono::steady_clock::now();

                        std::string date_str;
                        auto update_date_str = [&] {
                            auto last_time_t = time(0);
                            tm my_tm;

#if defined(_MSC_VER) || defined(__MINGW32__)
                            gmtime_s(&my_tm, &last_time_t);
#else
                            gmtime_r(&last_time_t, &my_tm);
#endif
                            date_str.resize(100);
                            size_t date_str_sz = strftime(&date_str[0], 99, "%a, %d %b %Y %H:%M:%S GMT", &my_tm);
                            date_str.resize(date_str_sz);
                        };
                        update_date_str();
                        get_cached_date_str_pool_[i] = [&]() -> std::string {
                            if (std::chrono::steady_clock::now() - last >= std::chrono::seconds(1))
                            {
                                last = std::chrono::steady_clock::now();
                                update_date_str();
                            }
                            return date_str;
                        };

                        // initializing task timers
                        detail::task_timer task_timer(*io_context_pool_[i]);
                        task_timer.set_default_timeout(timeout_);
                        task_timer_pool_[i] = &task_timer;
                        task_queue_length_pool_[i] = 0;

                        init_count++;
                        while (1)
                        {
                            try
                            {
                                if (io_context_pool_[i]->run() == 0)
                                {
                                    // when io_service.run returns 0, there are no more works to do.
                                    break;
                                }
                            }
                            catch (std::exception& e)
                            {
                                CROW_LOG_ERROR << "Worker Crash: An uncaught exception occurred: " << e.what();
                            }
                        }
                    }));

            if (tick_function_ && tick_interval_.count() > 0)
            {
                tick_timer_.expires_after(std::chrono::milliseconds(tick_interval_.count()));
                tick_timer_.async_wait(
                  [this](const error_code& ec) {
                      if (ec)
                          return;
                      on_tick();
                  });
            }

            handler_->port(acceptor_.local_endpoint().port());


            CROW_LOG_INFO << server_name_
                          << " server is running at " << (handler_->ssl_used() ? "https://" : "http://")
                          << acceptor_.local_endpoint().address() << ":" << acceptor_.local_endpoint().port() << " using " << concurrency_ << " threads";
            CROW_LOG_INFO << "Call `app.loglevel(crow::LogLevel::Warning)` to hide Info level logs.";

            signals_.async_wait(
              [&](const error_code& /*error*/, int /*signal_number*/) {
                  stop();
              });

            while (worker_thread_count != init_count)
                std::this_thread::yield();

            do_accept();

            std::thread(
              [this] {
                  notify_start();
                  io_context_.run();
                  CROW_LOG_INFO << "Exiting.";
              })
              .join();
        }

        void stop()
        {
            shutting_down_ = true; // Prevent the acceptor from taking new connections
            for (auto& io_context : io_context_pool_)
            {
                if (io_context != nullptr)
                {
                    CROW_LOG_INFO << "Closing IO service " << &io_context;
                    io_context->stop(); // Close all io_services (and HTTP connections)
                }
            }

            CROW_LOG_INFO << "Closing main IO service (" << &io_context_ << ')';
            io_context_.stop(); // Close main io_service
        }

        uint16_t port() const {
            return acceptor_.local_endpoint().port();
        }

        /// Wait until the server has properly started or until timeout
        std::cv_status wait_for_start(std::chrono::steady_clock::time_point wait_until)
        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            
            std::cv_status status = std::cv_status::no_timeout;
            while (!server_started_ && ( status==std::cv_status::no_timeout ))
                status = cv_started_.wait_until(lock,wait_until);
            return status;
        }

        void signal_clear()
        {
            signals_.clear();
        }

        void signal_add(int signal_number)
        {
            signals_.add(signal_number);
        }

    private:
        uint16_t pick_io_context_idx()
        {
            uint16_t min_queue_idx = 0;

            // TODO improve load balancing
            // size_t is used here to avoid the security issue https://codeql.github.com/codeql-query-help/cpp/cpp-comparison-with-wider-type/
            // even though the max value of this can be only uint16_t as concurrency is uint16_t.
            for (size_t i = 1; i < task_queue_length_pool_.size() && task_queue_length_pool_[min_queue_idx] > 0; i++)
            // No need to check other io_services if the current one has no tasks
            {
                if (task_queue_length_pool_[i] < task_queue_length_pool_[min_queue_idx])
                    min_queue_idx = i;
            }
            return min_queue_idx;
        }

        void do_accept()
        {
            if (!shutting_down_)
            {
                uint16_t context_idx = pick_io_context_idx();
                asio::io_context& ic = *io_context_pool_[context_idx];
                task_queue_length_pool_[context_idx]++;
                CROW_LOG_DEBUG << &ic << " {" << context_idx << "} queue length: " << task_queue_length_pool_[context_idx];

                auto p = std::make_shared<Connection<Adaptor, Handler, Middlewares...>>(
                  ic, handler_, server_name_, middlewares_,
                  get_cached_date_str_pool_[context_idx], *task_timer_pool_[context_idx], adaptor_ctx_, task_queue_length_pool_[context_idx]);

                acceptor_.async_accept(
                  p->socket(),
                  [this, p, &ic, context_idx](error_code ec) {
                      if (!ec)
                      {
                          asio::post(ic,
                            [p] {
                                p->start();
                            });
                      }
                      else
                      {
                          task_queue_length_pool_[context_idx]--;
                          CROW_LOG_DEBUG << &ic << " {" << context_idx << "} queue length: " << task_queue_length_pool_[context_idx];
                      }
                      do_accept();
                  });
            }
        }

        /// Notify anything using `wait_for_start()` to proceed
        void notify_start()
        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            server_started_ = true;
            cv_started_.notify_all();
        }

    private:
        std::vector<std::unique_ptr<asio::io_context>> io_context_pool_;
        asio::io_context io_context_;
        std::vector<detail::task_timer*> task_timer_pool_;
        std::vector<std::function<std::string()>> get_cached_date_str_pool_;
        tcp::acceptor acceptor_;
        bool shutting_down_ = false;
        bool server_started_{false};
        std::condition_variable cv_started_;
        std::mutex start_mutex_;
        asio::signal_set signals_;

        asio::basic_waitable_timer<std::chrono::high_resolution_clock> tick_timer_;

        Handler* handler_;
        uint16_t concurrency_{2};
        std::uint8_t timeout_;
        std::string server_name_;
        std::vector<std::atomic<unsigned int>> task_queue_length_pool_;

        std::chrono::milliseconds tick_interval_;
        std::function<void()> tick_function_;

        std::tuple<Middlewares...>* middlewares_;

        typename Adaptor::context* adaptor_ctx_;
    };
} // namespace crow
