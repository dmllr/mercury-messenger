#pragma once

#include <any>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include <ranges>

#include "eillance/utils/dispatch_queue.hpp"

namespace eillance {

class EventQueue {
 public:
  using SubscriberTypeId = std::size_t;

 private:
  std::mutex syncMutex_;
  std::mutex conditionMutex_;
  std::condition_variable condition_;
  std::deque<std::pair<std::type_index, std::any>> events_{};
  std::unordered_map<std::type_index, std::vector<std::pair<SubscriberTypeId, std::function<void(std::any const&)>>>>
      subscribers_{};
  DispatchQueue dispatchQueue_ = DispatchQueue("EventQueue", 1);
  SubscriberTypeId nextId_ = 0;  // For unique callback identification

  struct TerminationEvent {
    bool terminate{true};
  };

 public:
  static EventQueue& Instance() {
    static auto instance{EventQueue()};
    return instance;
  }

  EventQueue() {
    dispatchQueue_.Dispatch([this]() { DispatchEvents(); });
  }
  EventQueue(EventQueue&&) = delete;
  EventQueue(EventQueue const&) = delete;
  EventQueue& operator=(EventQueue&&) = delete;
  EventQueue& operator=(EventQueue const&) = delete;

  ~EventQueue() {
    dispatchQueue_.RemovePending();
    Push(TerminationEvent{});
    condition_.notify_one();
  }

  /// Subscribe to events of a specific type.
  template <typename EventType>
  SubscriberTypeId Subscribe(std::function<void(EventType const&)> const& callback) {
    auto const lock = std::lock_guard<std::mutex>(syncMutex_);
    SubscriberTypeId const id = nextId_++;
    subscribers_[typeid(EventType)].emplace_back(id, [callback](std::any const& event) {
      callback(std::any_cast<EventType>(event));
    });
    return id;
  }

  /// Unsubscribe from events of a specific type.
  template <typename EventType>
  void Unsubscribe(SubscriberTypeId id) {
    auto const lock = std::lock_guard<std::mutex>(syncMutex_);
    auto& vec = subscribers_[typeid(EventType)];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [id](auto const& pair) { return pair.first == id; }), vec.end());
  }

  /// Push an event to the queue.
  template <typename EventType>
  void Push(EventType const& event) {
    {
      std::lock_guard<std::mutex> const lock(syncMutex_);
      events_.push_back(MakePair(typeid(EventType), std::move(event)));
    }
    condition_.notify_one();
  }

  /// Dispatch events to subscribers.
  void DispatchEvents() {
    // Wait for an event to be pushed.
    {
      std::unique_lock<std::mutex> lock(conditionMutex_);
      condition_.wait(lock, [this] { return !events_.empty(); });
    }

    // Pop the event from the queue.
    std::type_index type{typeid(int)};
    std::any contents;
    {
      std::lock_guard<std::mutex> const lock(syncMutex_);
      type = events_.front().first;
      contents = events_.front().second;
      events_.pop_front();
    }

    // Find subscribers for the event type.
    if (subscribers_.contains(type)) {
      auto& subscribers = subscribers_.at(type);

      // Dispatch the event to subscribers.
      for (auto const& callback : subscribers | std::views::values) {
        callback(contents);
      }
    }

    dispatchQueue_.Dispatch([this]() { DispatchEvents(); });
  }

 private:
  template <typename FirstType, typename SecondType>
  std::pair<FirstType, SecondType> MakePair(FirstType&& first, SecondType&& second) {
    return std::pair<FirstType, SecondType>(std::forward<FirstType>(first), std::forward<SecondType>(second));
  }
};

}  // namespace eillance
