#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::utils {

class Watchdog {
 public:
  template <typename Rep, typename Period>
  Watchdog(DispatchQueue& target_queue, std::chrono::duration<Rep, Period> timeout, std::function<void()> on_stall)
      : target_queue_(target_queue),
        timeout_us_(std::chrono::duration_cast<std::chrono::microseconds>(timeout).count()),
        on_stall_(std::move(on_stall)),
        last_heartbeat_(std::make_shared<std::atomic<uint64_t>>(0)) {
  }

  ~Watchdog() {
    Stop();
  }

  void Start() {
    if (running_.exchange(true)) {
      return;
    }
    *last_heartbeat_ = Time::SteadyNow();
    thread_ = std::thread([this]() -> void { MonitorLoop(); });
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  void MonitorLoop() {
    auto last_wake = Time::SteadyNow();

    while (running_) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        // Wake up every 5 seconds to perform a lightweight health check
        if (cv_.wait_for(lock, std::chrono::seconds(5), [this]() -> bool { return !running_; })) {
          break;
        }
      }

      auto const now = Time::SteadyNow();
      auto const time_since_last_wake = now - last_wake;
      last_wake = now;

      auto heartbeat_ptr = last_heartbeat_;
      target_queue_.Dispatch([heartbeat_ptr] { *heartbeat_ptr = Time::SteadyNow(); });

      // If the watchdog thread itself was delayed significantly (e.g., system suspend,
      // debugger breakpoint, or extreme CPU starvation), ignore this cycle to avoid false positives.
      if (time_since_last_wake > 10 * Time::uS) {
        continue;
      }

      if (now > heartbeat_ptr->load() + timeout_us_) {
        if (on_stall_) {
          on_stall_();
        }

        // Wait extra time before re-triggering to prevent spamming
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::seconds(10), [this]() -> bool { return !running_; })) {
          break;
        }
        last_wake = Time::SteadyNow();
      }
    }
  }

  DispatchQueue& target_queue_;
  uint64_t timeout_us_;
  std::function<void()> on_stall_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::condition_variable cv_;
  std::mutex mutex_;
  std::shared_ptr<std::atomic<uint64_t>> last_heartbeat_;
};

}  // namespace eillance::utils
