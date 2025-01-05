#pragma once

#ifdef CROW_USE_BOOST
#include <boost/asio.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#else
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#include <asio/basic_waitable_timer.hpp>
#endif

#include <chrono>
#include <functional>
#include <map>
#include <vector>

#include "crow/logging.h"

namespace crow
{
#ifdef CROW_USE_BOOST
    namespace asio = boost::asio;
    using error_code = boost::system::error_code;
#else
    using error_code = asio::error_code;
#endif
    namespace detail
    {

        /// A class for scheduling functions to be called after a specific
        /// amount of ticks. Ther tick length can  be handed over in constructor, 
        /// the default tick length is equal to 1 second.
        class task_timer
        {
        public:
            using task_type = std::function<void()>;
            using identifier_type = size_t;

        private:
            using clock_type = std::chrono::steady_clock;
            using time_type = clock_type::time_point;
        public:
            task_timer(asio::io_context& io_context,
                       const std::chrono::milliseconds tick_length =
                            std::chrono::seconds(1)) :
              io_context_(io_context), timer_(io_context_),
              tick_length_ms_(tick_length)
            {
                timer_.expires_after(tick_length_ms_);
                timer_.async_wait(
                  std::bind(&task_timer::tick_handler, this,
                  std::placeholders::_1));
            }

            ~task_timer() { timer_.cancel(); }

            /// Cancel the scheduling of the given task 
            ///
            /// \param identifier_type task identifier of the task to cancel.
            void cancel(identifier_type id)
            {
                tasks_.erase(id);
                CROW_LOG_DEBUG << "task_timer task cancelled: " << this << ' ' << id;
            }

            /// Schedule the given task to be executed after the default amount
            /// of ticks.

            ///
            /// \return identifier_type Used to cancel the thread.
            /// It is not bound to this task_timer instance and in some cases
            /// could lead to undefined behavior if used with other task_timer
            /// objects or after the task has been successfully executed.
            identifier_type schedule(const task_type& task)
            {
                return schedule(task, get_default_timeout());
            }

            /// Schedule the given task to be executed after the given time.

            ///
            /// \param timeout The amount of ticks to wait before execution.
            ///
            /// \return identifier_type Used to cancel the thread.
            /// It is not bound to this task_timer instance and in some cases
            /// could lead to undefined behavior if used with other task_timer
            /// objects or after the task has been successfully executed.
            identifier_type schedule(const task_type& task, uint8_t timeout)
            {
                tasks_.insert({++highest_id_,
                               {clock_type::now() + (timeout * tick_length_ms_),
                                task}});
                CROW_LOG_DEBUG << "task_timer scheduled: " << this << ' ' <<
                                  highest_id_;
                return highest_id_;
            }

            /// Set the default timeout for this task_timer instance.
            /// (Default: 5)

            ///
            /// \param timeout The amount of ticks to wait before
            /// execution. 
            /// For tick length \see tick_length_ms_ 
            void set_default_timeout(uint8_t timeout) {
                default_timeout_ = timeout;
            }

            /// Get the default timeout. (Default: 5)
            uint8_t get_default_timeout() const {
                return default_timeout_;
            }

            /// returns the length of one tick.
            std::chrono::milliseconds get_tick_length() const {
                return tick_length_ms_;
            }

        private:
            void process_tasks()
            {
                time_type current_time = clock_type::now();
                std::vector<identifier_type> finished_tasks;

                for (const auto& task : tasks_)
                {
                    if (task.second.first < current_time)
                    {
                        (task.second.second)();
                        finished_tasks.push_back(task.first);
                        CROW_LOG_DEBUG << "task_timer called: " << this <<
                                          ' ' << task.first;
                    }
                }

                for (const auto& task : finished_tasks)
                    tasks_.erase(task);

                // If no task is currently scheduled, reset the issued ids back
                // to 0.
                if (tasks_.empty()) highest_id_ = 0;
            }

            void tick_handler(const error_code& ec)
            {
                if (ec) return;

                process_tasks();

                timer_.expires_after(tick_length_ms_);
                timer_.async_wait(
                  std::bind(&task_timer::tick_handler, this, std::placeholders::_1));
            }

        private:
            asio::io_context& io_context_;
            asio::basic_waitable_timer<clock_type> timer_;
            std::map<identifier_type, std::pair<time_type, task_type>> tasks_;

            // A continuously increasing number to be issued to threads to
            // identify them. If no tasks are scheduled, it will be reset to 0.
            identifier_type highest_id_{0};
            std::chrono::milliseconds tick_length_ms_;
            uint8_t default_timeout_{5};

        };
    } // namespace detail
} // namespace crow
