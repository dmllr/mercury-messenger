#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance {

class EventQueue {
 public:
  using SubscriberTypeId = std::size_t;

 private:
  std::mutex events_mutex_;
  std::mutex subs_mutex_;
  std::mutex wait_mutex_;

  std::condition_variable sync_cv_;
  std::condition_variable wait_cv_;

  std::deque<std::pair<std::type_index, std::any>> events_{};
  std::unordered_map<std::type_index, std::vector<std::pair<SubscriberTypeId, std::function<void(std::any const&)>>>>
      subscribers_{};

  DispatchQueue dispatchQueue_ = DispatchQueue("EventQueue", 1);

  SubscriberTypeId nextId_ = 0;
  std::atomic<std::thread::id> dispatch_thread_id_{std::thread::id()};
  std::atomic<SubscriberTypeId> executing_subscriber_id_{0};

  struct TerminationEvent {
    bool terminate{true};
  };

 public:
  static auto Instance() -> EventQueue& {
    static auto instance{EventQueue()};
    return instance;
  }

  EventQueue() {
    dispatchQueue_.Dispatch([this]() -> void { DispatchLoop(); });
  }

  EventQueue(EventQueue&&) = delete;
  EventQueue(EventQueue const&) = delete;
  auto operator=(EventQueue&&) -> EventQueue& = delete;
  auto operator=(EventQueue const&) -> EventQueue& = delete;

  ~EventQueue() {
    dispatchQueue_.RemovePending();
    Push(TerminationEvent{});
  }

  template <typename EventType>
  auto Subscribe(std::function<void(EventType const&)> const& callback) -> SubscriberTypeId {
    auto const lock = std::lock_guard<std::mutex>(subs_mutex_);
    SubscriberTypeId const id = ++nextId_;  // Start IDs from 1 since 0 signifies 'no execution'
    subscribers_[typeid(EventType)].emplace_back(id, [callback](std::any const& event) -> void {
      callback(std::any_cast<EventType>(event));
    });
    return id;
  }

  template <typename EventType>
  void Unsubscribe(SubscriberTypeId id) {
    {
      auto const lock = std::lock_guard<std::mutex>(subs_mutex_);
      auto& vec = subscribers_[typeid(EventType)];
      vec.erase(
          std::remove_if(vec.begin(), vec.end(), [id](auto const& pair) -> bool { return pair.first == id; }),
          vec.end()
      );
    }

    // Prevent race conditions where unsubscription happens while the callback is currently executing.
    // If called from the dispatch thread itself, skipping the block prevents deadlocks.
    if (std::this_thread::get_id() != dispatch_thread_id_.load(std::memory_order_acquire)) {
      while (executing_subscriber_id_.load(std::memory_order_acquire) == id) {
        std::this_thread::yield();
      }
    }
  }

  template <typename EventType>
  void Push(EventType const& event) {
    bool was_empty = false;
    {
      std::lock_guard<std::mutex> const lock(events_mutex_);
      was_empty = events_.empty();
      events_.emplace_back(typeid(EventType), std::any(event));
    }

    // MPSC Optimization: Only notify the condition variable if we transitioned from empty to non-empty.
    // Drastically reduces kernel wake-up contention under high parallel loads.
    if (was_empty) {
      sync_cv_.notify_one();
    }
  }

  template <typename Func>
  auto WaitFor(Func condition, std::chrono::milliseconds const timeout) -> bool {
    auto const start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(wait_mutex_);

    while (!condition()) {
      if (auto now = std::chrono::steady_clock::now(); now - start > timeout) {
        return false;
      }
      wait_cv_.wait_for(lock, std::chrono::milliseconds(10));
    }
    return true;
  }

 private:
  void DispatchLoop() {
    dispatch_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);

    while (true) {
      std::deque<std::pair<std::type_index, std::any>> local_events;

      {
        std::unique_lock<std::mutex> lock(events_mutex_);
        sync_cv_.wait(lock, [this]() -> bool { return !events_.empty(); });
        // O(1) swap ensures the producer queue is unblocked immediately
        local_events.swap(events_);
      }

      bool terminate_found = false;

      for (auto& [fst, snd] : local_events) {
        auto const& type = fst;
        auto const& contents = snd;

        if (type == typeid(TerminationEvent)) {
          terminate_found = true;
          continue;  // Guarantee "all in MUST out" by finishing the current batch processing
        }

        std::vector<std::pair<SubscriberTypeId, std::function<void(std::any const&)>>> current_subs;
        {
          std::lock_guard<std::mutex> const lock(subs_mutex_);
          if (auto const it = subscribers_.find(type); it != subscribers_.end()) {
            current_subs = it->second;
          }
        }

        for (auto const& pair : current_subs) {
          SubscriberTypeId const current_id = pair.first;

          // Double-check that this subscriber hasn't been removed while we were yielding/executing others
          bool still_valid = false;
          {
            std::lock_guard<std::mutex> const lock(subs_mutex_);
            if (auto const it = subscribers_.find(type); it != subscribers_.end()) {
              for (auto const& sub : it->second) {
                if (sub.first == current_id) {
                  still_valid = true;
                  break;
                }
              }
            }
          }

          if (!still_valid) {
            continue;
          }

          executing_subscriber_id_.store(current_id, std::memory_order_release);

          try {
            pair.second(contents);
          } catch (std::exception const& e) {
            LGE << "EventQueue dispatch error: " << e.what() << " [Event Type: " << type.name() << "]" << std::endl;
          } catch (...) {
            LGE << "EventQueue dispatch error: Unknown exception [Event Type: " << type.name() << "]" << std::endl;
          }

          executing_subscriber_id_.store(0, std::memory_order_release);
        }
      }

      // Notify external observers once per batch instead of per-event to maximize processing speed
      {
        std::lock_guard<std::mutex> const lock(wait_mutex_);
        wait_cv_.notify_all();
      }

      if (terminate_found) {
        return;
      }
    }
  }
};

}  // namespace eillance
