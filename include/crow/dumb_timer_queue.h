#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>

#include "crow/logging.h"

namespace crow {
namespace detail {
/// Fast timer queue for fixed tick value.
class dumb_timer_queue {
 public:
  static int tick;
  using key = std::pair<dumb_timer_queue*, int>;

  void cancel(key& k) {
    auto self = k.first;
    k.first = nullptr;
    if (!self) return;

    unsigned int index = (unsigned int)(k.second - self->step_);
    if (index < self->dq_.size()) self->dq_[index].second = nullptr;
  }

  /// Add a function to the queue.
  key add(std::function<void()> f) {
    dq_.emplace_back(std::chrono::steady_clock::now(), std::move(f));
    int ret = step_ + dq_.size() - 1;

    CROW_LOG_DEBUG << "timer add inside: " << this << ' ' << ret;
    return {this, ret};
  }

  /// Process the queue: take functions out in time intervals and execute them.
  void process() {
    if (!io_service_) return;

    auto now = std::chrono::steady_clock::now();
    while (!dq_.empty()) {
      auto& x = dq_.front();
      if (now - x.first < std::chrono::seconds(tick)) break;
      if (x.second) {
        CROW_LOG_DEBUG << "timer call: " << this << ' ' << step_;
        // we know that timer handlers are very simple currenty; call here
        x.second();
      }
      dq_.pop_front();
      step_++;
    }
  }

  void set_io_service(boost::asio::io_service& io_service) {
    io_service_ = &io_service;
  }

  dumb_timer_queue() noexcept {}

 private:
  boost::asio::io_service* io_service_{};
  std::deque<std::pair<decltype(std::chrono::steady_clock::now()),
                       std::function<void()>>>
      dq_;
  int step_{};
};
}  // namespace detail
}  // namespace crow
