#pragma once

#include <any>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
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
  std::mutex syncMutex_;  // Single mutex for condition + data protection
  std::condition_variable condition_;
  std::deque<std::pair<std::type_index, std::any>> events_{};
  std::unordered_map<std::type_index, std::vector<std::pair<SubscriberTypeId, std::function<void(std::any const&)>>>>
      subscribers_{};
  DispatchQueue dispatchQueue_ = DispatchQueue("EventQueue", 1);
  SubscriberTypeId nextId_ = 0;

  struct TerminationEvent {
    bool terminate{true};
  };

 public:
  static auto Instance() -> EventQueue& {
    static auto instance{EventQueue()};
    return instance;
  }

  EventQueue() {
    dispatchQueue_.Dispatch([this]() { DispatchLoop(); });
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
    auto const lock = std::lock_guard<std::mutex>(syncMutex_);
    SubscriberTypeId const id = nextId_++;
    subscribers_[typeid(EventType)].emplace_back(id, [callback](std::any const& event) {
      callback(std::any_cast<EventType>(event));
    });
    return id;
  }

  template <typename EventType>
  void Unsubscribe(SubscriberTypeId id) {
    auto const lock = std::lock_guard<std::mutex>(syncMutex_);
    auto& vec = subscribers_[typeid(EventType)];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [id](auto const& pair) { return pair.first == id; }), vec.end());
  }

  template <typename EventType>
  void Push(EventType const& event) {
    {
      std::lock_guard<std::mutex> const lock(syncMutex_);
      events_.push_back(MakePair(typeid(EventType), std::move(event)));
    }
    condition_.notify_one();
  }

 private:
  void DispatchLoop() {
    while (true) {
      std::type_index type{typeid(int)};
      std::any contents;

      {
        std::unique_lock<std::mutex> lock(syncMutex_);
        condition_.wait(lock, [this] { return !events_.empty(); });

        type = events_.front().first;
        contents = events_.front().second;
        events_.pop_front();
      }

      if (type == typeid(TerminationEvent)) {
        return;
      }

      std::vector<std::function<void(std::any const&)>> callbacks_to_run;
      {
        std::lock_guard<std::mutex> const lock(syncMutex_);
        if (subscribers_.contains(type)) {
          auto const& subs = subscribers_.at(type);
          callbacks_to_run.reserve(subs.size());
          for (auto const& pair : subs) {
            callbacks_to_run.push_back(pair.second);
          }
        }
      }

      for (auto const& callback : callbacks_to_run) {
        try {
          callback(contents);
        } catch (std::exception const& e) {
          std::cerr << "EventQueue dispatch error: " << e.what() << " [Event Type: " << type.name() << "]" << std::endl;
        } catch (...) {
          std::cerr << "EventQueue dispatch error: Unknown exception [Event Type: " << type.name() << "]" << std::endl;
        }
      }
    }
  }

  template <typename FirstType, typename SecondType>
  auto MakePair(FirstType&& first, SecondType&& second) -> std::pair<FirstType, SecondType> {
    return std::pair<FirstType, SecondType>(std::forward<FirstType>(first), std::forward<SecondType>(second));
  }
};

}  // namespace eillance
