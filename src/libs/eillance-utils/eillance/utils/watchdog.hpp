#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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
        on_stall_(std::move(on_stall)) {
  }

  ~Watchdog() {
    Stop();
  }

  void Start() {
    if (running_.exchange(true)) {
      return;
    }
    last_heartbeat_ = Time::Now();
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
    while (running_) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        // Wake up every 5 seconds to perform a lightweight health check
        if (cv_.wait_for(lock, std::chrono::seconds(5), [this]() -> bool { return !running_; })) {
          break;
        }
      }

      auto const now = Time::Now();
      target_queue_.Dispatch([this] { last_heartbeat_ = Time::Now(); });

      if (now > last_heartbeat_.load() + timeout_us_) {
        if (on_stall_) {
          on_stall_();
        }

        // Wait extra time before re-triggering to prevent spamming
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::seconds(10), [this]() -> bool { return !running_; })) {
          break;
        }
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
  std::atomic<uint64_t> last_heartbeat_{0};
};

}  // namespace eillance::utils
