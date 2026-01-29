#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace eillance {

class DispatchQueue {
  using FpT = std::function<void()>;

  struct ScheduledTask {
    std::chrono::steady_clock::time_point execute_time;
    FpT task;
  };

  struct CompareScheduledTask {
    auto operator()(ScheduledTask const& left, ScheduledTask const& right) const -> bool {
      // Priority queue orders by "greatest", so we want earliest time to be "greatest".
      // Return true if left > right (left is later/smaller priority than right).
      return left.execute_time > right.execute_time;
    }
  };

 public:
  explicit DispatchQueue(std::string name, std::size_t thread_count)
      : name_(std::move(name)),
        threads_(thread_count) {
    for (auto& thread : threads_) {
      thread = std::thread(&DispatchQueue::DispatchThreadHandler, this);
    }
  }

  ~DispatchQueue() {
    {
      std::unique_lock<std::mutex> const lock(queueAccessMutex_);
      shutdownRequested_ = true;
    }
    condition_.notify_all();

    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  static auto MainThread() -> DispatchQueue& {
    static DispatchQueue instance("Main", 1);
    return instance;
  }

  void Dispatch(FpT const& operation) {
    if (shutdownRequested_ || !operation) return;
    {
      auto const now = std::chrono::steady_clock::now();
      std::unique_lock<std::mutex> const lock(queueAccessMutex_);
      queue_.push(ScheduledTask{now, operation});
    }
    condition_.notify_one();
  }

  void Dispatch(FpT&& operation) {
    if (shutdownRequested_ || !operation) return;
    {
      auto const now = std::chrono::steady_clock::now();
      std::unique_lock<std::mutex> const lock(queueAccessMutex_);
      queue_.push(ScheduledTask{now, std::move(operation)});
    }
    condition_.notify_one();
  }

  void DispatchAfter(std::chrono::milliseconds const delay, FpT const&& operation) {
    if (shutdownRequested_ || !operation) return;
    {
      auto const scheduled_time = std::chrono::steady_clock::now() + delay;
      std::unique_lock<std::mutex> const lock(queueAccessMutex_);
      queue_.push(ScheduledTask{scheduled_time, operation});
    }
    condition_.notify_one();
  }

  [[nodiscard]] auto Size() const -> std::size_t {
    return queue_.size();
  }

  void RemovePending() {
    std::unique_lock<std::mutex> const lock(queueAccessMutex_);
    queue_ = decltype(queue_){};
  }

  DispatchQueue(DispatchQueue const& rhs) = delete;
  auto operator=(DispatchQueue const& rhs) -> DispatchQueue& = delete;
  DispatchQueue(DispatchQueue&& rhs) = delete;
  auto operator=(DispatchQueue&& rhs) -> DispatchQueue& = delete;

 private:
  std::string name_;
  std::mutex queueAccessMutex_;
  std::vector<std::thread> threads_{};
  std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, CompareScheduledTask> queue_{};
  std::condition_variable condition_;
  bool shutdownRequested_ = false;

  void DispatchThreadHandler() {
    std::unique_lock<std::mutex> lock(queueAccessMutex_, std::defer_lock);

    while (!shutdownRequested_) {
      lock.lock();

      if (queue_.empty()) {
        condition_.wait(lock, [this] { return shutdownRequested_ || !queue_.empty(); });
        if (queue_.empty()) {
          lock.unlock();
          continue;
        }
      }

      auto now = std::chrono::steady_clock::now();
      // Safe copy of the top element's execution time to avoid invalidating reference if queue changes
      auto next_execute_time = queue_.top().execute_time;

      if (next_execute_time > now) {
        condition_.wait_until(lock, next_execute_time, [this, next_execute_time] {
          if (shutdownRequested_) return true;
          if (queue_.empty()) return false;
          // Accessing top() is safe here because we hold the lock when predicate runs
          return queue_.top().execute_time < next_execute_time;
        });

        lock.unlock();
        continue;
      }

      auto task = queue_.top().task;
      queue_.pop();
      lock.unlock();

      try {
        if (task) task();
      } catch (...) {
      }
    }
  }
};

}  // namespace eillance
