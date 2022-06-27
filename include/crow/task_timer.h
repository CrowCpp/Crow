#pragma once

#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE
#endif
#include <asio.hpp>
#include <asio/basic_waitable_timer.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <vector>

#include "crow/logging.h"

namespace crow
{
    namespace detail
    {

        /// A class for scheduling functions to be called after a specific amount of ticks. A tick is equal to 1 second.
        class task_timer
        {
        public:
            using task_type = std::function<void()>;
            using identifier_type = size_t;

        private:
            using clock_type = std::chrono::steady_clock;
            using time_type = clock_type::time_point;

        public:
            task_timer(asio::io_service& io_service):
              io_service_(io_service), timer_(io_service_)
            {
                timer_.expires_after(std::chrono::seconds(1));
                timer_.async_wait(
                  std::bind(&task_timer::tick_handler, this, std::placeholders::_1));
            }

            ~task_timer() { timer_.cancel(); }

            void cancel(identifier_type id)
            {
                tasks_.erase(id);
                CROW_LOG_DEBUG << "task_timer cancelled: " << this << ' ' << id;
            }

            /// Schedule the given task to be executed after the default amount of ticks.

            ///
            /// \return identifier_type Used to cancel the thread.
            /// It is not bound to this task_timer instance and in some cases could lead to
            /// undefined behavior if used with other task_timer objects or after the task
            /// has been successfully executed.
            identifier_type schedule(const task_type& task)
            {
                tasks_.insert(
                  {++highest_id_,
                   {clock_type::now() + std::chrono::seconds(get_default_timeout()),
                    task}});
                CROW_LOG_DEBUG << "task_timer scheduled: " << this << ' ' << highest_id_;
                return highest_id_;
            }

            /// Schedule the given task to be executed after the given time.

            ///
            /// \param timeout The amount of ticks (seconds) to wait before execution.
            ///
            /// \return identifier_type Used to cancel the thread.
            /// It is not bound to this task_timer instance and in some cases could lead to
            /// undefined behavior if used with other task_timer objects or after the task
            /// has been successfully executed.
            identifier_type schedule(const task_type& task, std::uint8_t timeout)
            {
                tasks_.insert({++highest_id_,
                               {clock_type::now() + std::chrono::seconds(timeout), task}});
                CROW_LOG_DEBUG << "task_timer scheduled: " << this << ' ' << highest_id_;
                return highest_id_;
            }

            /// Set the default timeout for this task_timer instance. (Default: 5)

            ///
            /// \param timeout The amount of ticks (seconds) to wait before execution.
            void set_default_timeout(std::uint8_t timeout) { default_timeout_ = timeout; }

            /// Get the default timeout. (Default: 5)
            std::uint8_t get_default_timeout() const { return default_timeout_; }

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
                        CROW_LOG_DEBUG << "task_timer called: " << this << ' ' << task.first;
                    }
                }

                for (const auto& task : finished_tasks)
                    tasks_.erase(task);

                // If no task is currently scheduled, reset the issued ids back to 0.
                if (tasks_.empty()) highest_id_ = 0;
            }

            void tick_handler(const asio::error_code& ec)
            {
                if (ec) return;

                process_tasks();

                timer_.expires_after(std::chrono::seconds(1));
                timer_.async_wait(
                  std::bind(&task_timer::tick_handler, this, std::placeholders::_1));
            }

        private:
            std::uint8_t default_timeout_{5};
            asio::io_service& io_service_;
            asio::basic_waitable_timer<clock_type> timer_;
            std::map<identifier_type, std::pair<time_type, task_type>> tasks_;

            // A continuosly increasing number to be issued to threads to identify them.
            // If no tasks are scheduled, it will be reset to 0.
            identifier_type highest_id_{0};
        };
    } // namespace detail
} // namespace crow
