#pragma once

#include <any>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "hela/eillance/common/dispatch_queue.hpp"

namespace hela::eillance {

class EventQueue {
public:
  using SubscriberTypeId = std::size_t;

private:
  std::mutex sync_mutex_;
  std::mutex condition_mutex_;
  std::condition_variable condition_;
  std::deque<std::pair<std::type_index, std::any>> events_;
  std::unordered_map<std::type_index, std::vector<std::pair<SubscriberTypeId, std::function<void(const std::any&)>>>> subscribers_;
  DispatchQueue dispatchQueue_ = DispatchQueue("EventQueue", 1);
  SubscriberTypeId nextId_ = 0; // For unique callback identification

  struct TerminationEvent {
    bool terminate{true};
  };

public:
  static EventQueue& Instance() {
    static auto instance{EventQueue()};
    return instance;
  }

  EventQueue() {
    dispatchQueue_.dispatch([this]() { DispatchEvents(); });
  }
  EventQueue(EventQueue&&) = delete;
  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(EventQueue&&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;

  ~EventQueue() {
    dispatchQueue_.removePending();
    Push(TerminationEvent{});
    condition_.notify_one();
  }

  /// Subscribe to events of a specific type.
  template <typename EventType>
  SubscriberTypeId Subscribe(const std::function<void(const EventType&)>& callback) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    SubscriberTypeId id = nextId_++;
    subscribers_[typeid(EventType)].emplace_back(id, [callback](const std::any& event) {
      callback(std::any_cast<EventType>(event));
    });
    return id;
  }

  /// Unsubscribe from events of a specific type.
  template <typename EventType>
  void Unsubscribe(SubscriberTypeId id) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    auto& vec = subscribers_[typeid(EventType)];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [id](const auto& pair) {
                return pair.first == id;
              }), vec.end());
  }

  /// Push an event to the queue.
  template <typename EventType>
  void Push(const EventType& event) {
    {
      std::lock_guard<std::mutex> const lock(sync_mutex_);
      events_.push_back(MakePair(typeid(EventType), event));
    }
    condition_.notify_one();
  }

  /// Dispatch events to subscribers.
  void DispatchEvents() {
    // Wait for an event to be pushed.
    {
      std::unique_lock<std::mutex> lock(condition_mutex_);
      condition_.wait(lock, [this] { return !events_.empty(); });
    }

    // Pop the event from the queue.
    auto [type, contents] = events_.front();
    events_.pop_front();

    // Find subscribers for the event type.
    if (subscribers_.find(type) != subscribers_.end()) {
      auto& subscribers = subscribers_.at(type);

      // Dispatch the event to subscribers.
      for (const auto& [id, callback]: subscribers) {
        callback(contents);
      }
    }

    dispatchQueue_.dispatch([this]() { DispatchEvents(); });
  }

private:
  template <typename FirstType, typename SecondType>
  std::pair<FirstType, SecondType> MakePair(FirstType&& first, SecondType&& second) {
    return std::pair<FirstType, SecondType>(std::forward<FirstType>(first), std::forward<SecondType>(second));
  }
};

} // namespace hela::eillance
