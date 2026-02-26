#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "eillance/utils/dispatch_queue.hpp"

namespace eillance {

class EventQueue {
 public:
  using SubscriberTypeId = std::size_t;

 private:
  std::mutex sync_mutex_;
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
    auto const lock = std::lock_guard<std::mutex>(sync_mutex_);
    SubscriberTypeId const id = ++nextId_;  // Start IDs from 1 since 0 signifies 'no execution'
    subscribers_[typeid(EventType)].emplace_back(id, [callback](std::any const& event) -> void {
      callback(std::any_cast<EventType>(event));
    });
    return id;
  }

  template <typename EventType>
  void Unsubscribe(SubscriberTypeId id) {
    {
      auto const lock = std::lock_guard<std::mutex>(sync_mutex_);
      auto& vec = subscribers_[typeid(EventType)];
      vec.erase(
          std::remove_if(vec.begin(), vec.end(), [id](auto const& pair) -> bool { return pair.first == id; }),
          vec.end()
      );
    }

    // Prevent race conditions where unsubscription happens while the callback is currently executing.
    // If called from the dispatch thread itself, skipping the block prevents deadlocks.
    if (std::this_thread::get_id() != dispatch_thread_id_.load()) {
      while (executing_subscriber_id_.load() == id) {
        std::this_thread::yield();
      }
    }
  }

  template <typename EventType>
  void Push(EventType const& event) {
    {
      std::lock_guard<std::mutex> const lock(sync_mutex_);
      events_.push_back(MakePair(typeid(EventType), std::move(event)));
    }
    sync_cv_.notify_one();
  }

  template <typename Func>
  auto WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) -> bool {
    std::mutex wait_mutex;
    auto const start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(wait_mutex);
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
    dispatch_thread_id_.store(std::this_thread::get_id());

    while (true) {
      std::type_index type{typeid(int)};
      std::any contents;

      {
        std::unique_lock<std::mutex> lock(sync_mutex_);
        sync_cv_.wait(lock, [this]() -> bool { return !events_.empty(); });

        type = events_.front().first;
        contents = events_.front().second;
        events_.pop_front();
      }

      if (type == typeid(TerminationEvent)) {
        return;
      }

      std::vector<std::pair<SubscriberTypeId, std::function<void(std::any const&)>>> current_subs;
      {
        std::lock_guard<std::mutex> const lock(sync_mutex_);
        if (subscribers_.contains(type)) {
          current_subs = subscribers_.at(type);
        }
      }

      for (auto const& pair : current_subs) {
        SubscriberTypeId const current_id = pair.first;

        // Double-check that this subscriber hasn't been removed while we were yielding/executing others
        bool still_valid = false;
        {
          std::lock_guard<std::mutex> const lock(sync_mutex_);
          if (subscribers_.contains(type)) {
            auto const& master_list = subscribers_.at(type);
            for (auto const& sub : master_list) {
              if (sub.first == current_id) {
                still_valid = true;
                break;
              }
            }
          }
        }

        if (!still_valid) {
          continue;  // It was unsubscribed after we made our local copy!
        }

        executing_subscriber_id_.store(current_id);

        try {
          pair.second(contents);
        } catch (std::exception const& e) {
          std::cerr << "EventQueue dispatch error: " << e.what() << " [Event Type: " << type.name() << "]" << std::endl;
        } catch (...) {
          std::cerr << "EventQueue dispatch error: Unknown exception [Event Type: " << type.name() << "]" << std::endl;
        }

        executing_subscriber_id_.store(0);
      }
      wait_cv_.notify_all();
    }
  }

  template <typename FirstType, typename SecondType>
  auto MakePair(FirstType&& first, SecondType&& second) -> std::pair<FirstType, SecondType> {
    return std::pair<FirstType, SecondType>(std::forward<FirstType>(first), std::forward<SecondType>(second));
  }
};

}  // namespace eillance
