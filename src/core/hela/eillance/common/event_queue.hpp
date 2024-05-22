#pragma once

#include <any>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <typeindex>

#include "hela/eillance/common/dispatch_queue.hpp"

namespace hela::eillance {

class EventQueue {
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<std::pair<std::type_index, std::any>> events_;
  std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> subscribers_;
  DispatchQueue dispatchQueue_ = DispatchQueue("EventQueue", 1);

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

  // Subscribe to events of a specific type.
  template <typename EventType>
  void Subscribe(const std::function<void(const EventType&)>& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[typeid(EventType)].push_back([callback](const std::any& event) {
      callback(std::any_cast<EventType>(event));
    });
  }

  // Push an event to the queue.
  template <typename EventType>
  void Push(const EventType& event) {
    std::lock_guard<std::mutex> const lock(mutex_);
    events_.push_back(MakePair(typeid(EventType), event));
    condition_.notify_one();
  }

  // Dispatch events to subscribers.
  void DispatchEvents() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait for an event to be pushed.
    condition_.wait(lock, [this] { return !events_.empty(); });

    // Pop the event from the queue.
    auto event = events_.front();
    events_.pop_front();

    // Find subscribers for the event type.
    auto& subscribers = subscribers_[event.first];

    // Dispatch the event to subscribers.
    for (auto& callback : subscribers) {
      callback(std::get<1>(event));
    }

    dispatchQueue_.dispatch([this]() { DispatchEvents(); });
  }

private:
  template <typename FirstType, typename SecondType>
  std::pair<FirstType, SecondType> MakePair(FirstType&& first, SecondType&& second) {
    return std::pair<FirstType, SecondType>(std::forward<FirstType>(first), std::forward<SecondType>(second));
  }};

} // namespace hela::eillance
