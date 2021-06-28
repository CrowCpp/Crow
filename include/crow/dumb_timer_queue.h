/*
Copyright (c) 2014-2017, ipkn
              2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <boost/asio.hpp>
#include <deque>
#include <functional>
#include <chrono>
#include <thread>

#include "crow/logging.h"

namespace crow
{
    namespace detail 
    {

        /// Fast timer queue for fixed tick value.
        class dumb_timer_queue
        {
        public:
            static int tick;
            using key = std::pair<dumb_timer_queue*, int>;

            void cancel(key& k)
            {
                auto self = k.first;
                k.first = nullptr;
                if (!self)
                    return;

                unsigned int index = static_cast<unsigned>(k.second - self->step_);
                if (index < self->dq_.size())
                    self->dq_[index].second = nullptr;
            }

            /// Add a function to the queue.
            key add(std::function<void()> f)
            {
                dq_.emplace_back(std::chrono::steady_clock::now(), std::move(f));
                int ret = step_+dq_.size()-1;

                CROW_LOG_DEBUG << "timer add inside: " << this << ' ' << ret ;
                return {this, ret};
            }

            /// Process the queue: take functions out in time intervals and execute them.
            void process()
            {
                if (!io_service_)
                    return;

                auto now = std::chrono::steady_clock::now();
                while(!dq_.empty())
                {
                    auto& x = dq_.front();
                    if (now - x.first < std::chrono::seconds(tick))
                        break;
                    if (x.second)
                    {
                        CROW_LOG_DEBUG << "timer call: " << this << ' ' << step_;
                        // we know that timer handlers are very simple currenty; call here
                        x.second();
                    }
                    dq_.pop_front();
                    step_++;
                }
            }

            void set_io_service(boost::asio::io_service& io_service)
            {
                io_service_ = &io_service;
            }

            dumb_timer_queue() noexcept
            {
            }

        private:
            boost::asio::io_service* io_service_{};
            std::deque<std::pair<decltype(std::chrono::steady_clock::now()), std::function<void()>>> dq_;
            int step_{};
        };
    }
}
