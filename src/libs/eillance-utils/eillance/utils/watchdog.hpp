#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::utils {

struct EventWatchdogHeartbeat {
  std::uint32_t target_id{0};
};

class Watchdog final {
 public:
  using StallCallback = std::function<void(std::string const&)>;

  explicit Watchdog(StallCallback on_stall)
      : on_stall_(std::move(on_stall)) {
  }

  ~Watchdog() {
    Stop();
  }

  Watchdog(Watchdog const&) = delete;
  Watchdog(Watchdog&&) = delete;
  auto operator=(Watchdog const&) -> Watchdog& = delete;
  auto operator=(Watchdog&&) -> Watchdog& = delete;

  template <typename Rep1, typename Period1, typename Rep2, typename Period2>
  auto Register(
      std::string name,
      std::chrono::duration<Rep1, Period1> timeout,
      std::chrono::duration<Rep2, Period2> expected_interval
  ) -> std::uint32_t {
    auto target = std::make_shared<Target>();
    target->id = next_id_++;
    target->name = std::move(name);
    target->timeout_us = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
    target->expected_interval_us = std::chrono::duration_cast<std::chrono::microseconds>(expected_interval).count();
    target->last_feed.store(Time::SteadyNow(), std::memory_order_relaxed);
    target->active.store(true, std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> const lock(targets_mutex_);
      targets_[target->id] = target;
    }
    cv_.notify_one();
    return target->id;
  }

  void Unregister(uint32_t const id) {
    std::lock_guard<std::mutex> const lock(targets_mutex_);
    targets_.erase(id);
  }

  void Feed(uint32_t const id) {
    std::shared_ptr<Target> target;
    {
      std::lock_guard<std::mutex> const lock(targets_mutex_);
      if (auto const it = targets_.find(id); it != targets_.end()) {
        target = it->second;
      }
    }
    if (target) {
      target->last_feed.store(Time::SteadyNow(), std::memory_order_release);
    }
  }

  template <typename Rep1, typename Period1, typename Rep2, typename Period2>
  void MonitorQueue(
      std::string name,
      DispatchQueue& queue,
      std::chrono::duration<Rep1, Period1> timeout,
      std::chrono::duration<Rep2, Period2> expected_interval
  ) {
    auto const exp_interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(expected_interval);
    auto const target_id = Register(std::move(name), timeout, expected_interval);

    auto feeder = std::make_shared<std::function<void()>>();
    auto is_active = std::make_shared<std::atomic<bool>>(true);
    active_feeder_tokens_.push_back(is_active);

    *feeder = [this, &queue, target_id, exp_interval_ms, feeder, is_active]() -> void {
      if (!is_active->load(std::memory_order_relaxed) || !running_.load(std::memory_order_relaxed)) {
        return;
      }

      if (queue.Size() == 0) {
        Feed(target_id);
        queue.DispatchAfter(exp_interval_ms, [feeder]() -> void { (*feeder)(); });
      } else {
        queue.Dispatch([this, &queue, target_id, exp_interval_ms, feeder, is_active]() -> void {
          if (!is_active->load(std::memory_order_relaxed) || !running_.load(std::memory_order_relaxed)) {
            return;
          }
          Feed(target_id);
          queue.DispatchAfter(exp_interval_ms, [feeder]() -> void { (*feeder)(); });
        });
      }
    };

    queue.DispatchAfter(exp_interval_ms, [feeder]() -> void { (*feeder)(); });
  }

  template <typename Rep1, typename Period1, typename Rep2, typename Period2>
  void MonitorEventQueue(
      EventQueue& event_queue,
      std::chrono::duration<Rep1, Period1> timeout,
      std::chrono::duration<Rep2, Period2> expected_interval
  ) {
    auto const target_id = Register("EventQueue", timeout, expected_interval);
    auto const exp_interval_us =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(expected_interval).count());

    auto const sub_id =
        event_queue.Subscribe<EventWatchdogHeartbeat>([this](EventWatchdogHeartbeat const& heartbeat) -> void {
          Feed(heartbeat.target_id);
        });

    cleanup_actions_.emplace_back([&event_queue, sub_id]() -> void { event_queue.Unsubscribe(sub_id); });

    monitored_event_queues_.push_back({&event_queue, target_id, exp_interval_us, 0});
  }

  void Start() {
    if (running_.exchange(true)) {
      return;
    }
    thread_ = std::thread([this]() -> void { MonitorLoop(); });
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }

    for (auto const& token : active_feeder_tokens_) {
      token->store(false, std::memory_order_relaxed);
    }
    active_feeder_tokens_.clear();

    for (auto const& cleanup : cleanup_actions_) {
      cleanup();
    }
    cleanup_actions_.clear();
    monitored_event_queues_.clear();

    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }

    std::lock_guard<std::mutex> const lock(targets_mutex_);
    targets_.clear();
  }

 private:
  struct Target {
    std::uint32_t id{0};
    std::string name;
    std::uint64_t timeout_us{0};
    std::uint64_t expected_interval_us{0};
    std::atomic<uint64_t> last_feed{0};
    std::atomic<bool> active{true};
  };

  struct MonitoredEventQueue {
    EventQueue* queue{nullptr};
    std::uint32_t target_id{0};
    std::uint64_t expected_interval_us{0};
    std::uint64_t last_push{0};
  };

  void MonitorLoop() {
    auto last_wake = Time::SteadyNow();

    while (running_) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(1000), [this]() -> bool { return !running_; })) {
          break;
        }
      }

      auto const now = Time::SteadyNow();
      auto const time_since_last_wake = now - last_wake;
      last_wake = now;

      if (time_since_last_wake > 10 * Time::uS) {
        std::lock_guard<std::mutex> const lock(targets_mutex_);
        for (auto const& target : targets_ | std::views::values) {
          target->last_feed.store(now, std::memory_order_relaxed);
        }
        continue;
      }

      for (auto& [queue, target_id, expected_interval_us, last_push] : monitored_event_queues_) {
        if (now > last_push + expected_interval_us) {
          last_push = now;
          queue->Push(EventWatchdogHeartbeat{target_id});
        }
      }

      std::string stalled_target_name;
      bool has_stall = false;

      {
        std::lock_guard<std::mutex> const lock(targets_mutex_);
        for (auto const& target : targets_ | std::views::values) {
          if (!target->active.load(std::memory_order_relaxed)) {
            continue;
          }
          if (auto const feed_time = target->last_feed.load(std::memory_order_acquire);
              now > feed_time + target->timeout_us) {
            has_stall = true;
            stalled_target_name = target->name;
            target->last_feed.store(now, std::memory_order_relaxed);
            break;
          }
        }
      }

      if (has_stall && on_stall_) {
        on_stall_(stalled_target_name);

        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::seconds(10), [this]() -> bool { return !running_; })) {
          break;
        }
        last_wake = Time::SteadyNow();
      }
    }
  }

  StallCallback on_stall_;
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> next_id_{1};

  std::thread thread_;
  std::condition_variable cv_;
  std::mutex mutex_;

  std::unordered_map<uint32_t, std::shared_ptr<Target>> targets_;
  std::mutex targets_mutex_;

  std::vector<std::shared_ptr<std::atomic<bool>>> active_feeder_tokens_;
  std::vector<std::function<void()>> cleanup_actions_;
  std::vector<MonitoredEventQueue> monitored_event_queues_;
};

}  // namespace eillance::utils
