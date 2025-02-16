#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace hela::eillance {

class DispatchQueue {
  using FpT = std::function<void()>;

  // Structure to hold a scheduled task
  struct ScheduledTask {
    std::chrono::steady_clock::time_point executeTime;
    FpT task;
  };

  // Comparator for the priority queue (earliest executeTime has highest priority)
  struct CompareScheduledTask {
    bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
      return a.executeTime > b.executeTime;
    }
  };

public:
  explicit DispatchQueue(std::string name, std::size_t threadCount)
      : name_(std::move(name)), threads_(threadCount) {
    for (auto& thread : threads_) {
      thread = std::thread(&DispatchQueue::DispatchThreadHandler, this);
    }
  }

  ~DispatchQueue() {
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      quit_ = true;
    }
    condition_.notify_all();

    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  static DispatchQueue& MainThread() {
    static DispatchQueue instance("Main", 1);
    return instance;
  }

  /// Dispatch an operation immediately.
  void Dispatch(const FpT& op) {
    auto now = std::chrono::steady_clock::now();
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{now, op});
    }
    condition_.notify_one();
  }

  /// Dispatch an operation immediately.
  void Dispatch(FpT&& op) {
    auto now = std::chrono::steady_clock::now();
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{now, std::move(op)});
    }
    condition_.notify_one();
  }

  /// Dispatch an operation to be executed after a delay.
  void DispatchAfter(std::chrono::milliseconds delay, FpT op) {
    auto scheduled_time = std::chrono::steady_clock::now() + delay;
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{scheduled_time, std::move(op)});
    }
    condition_.notify_one();
  }

  /// Remove any pending tasks.
  void RemovePending() {
    std::unique_lock<std::mutex> lock(lockMutex_);
    queue_ = decltype(queue_){};
  }

  /// Delete copy and move constructors/assignments.
  DispatchQueue(const DispatchQueue& rhs) = delete;
  auto operator=(const DispatchQueue& rhs) -> DispatchQueue& = delete;
  DispatchQueue(DispatchQueue&& rhs) = delete;
  auto operator=(DispatchQueue&& rhs) -> DispatchQueue& = delete;

private:
  std::string name_;
  std::mutex lockMutex_;
  std::vector<std::thread> threads_;
  /// Priority queue to store scheduled tasks
  std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, CompareScheduledTask> queue_;
  std::condition_variable condition_;
  bool quit_ = false;

  /// Thread handler that waits until tasks are ready to execute.
  void DispatchThreadHandler() {
    std::unique_lock<std::mutex> lock(lockMutex_);
    while (!quit_) {
      if (queue_.empty()) {
        // Wait until a task is added or the queue is shutting down.
        condition_.wait(lock, [this] { return quit_ || !queue_.empty(); });
      } else {
        auto now = std::chrono::steady_clock::now();
        auto next_task_time = queue_.top().executeTime;
        if (next_task_time > now) {
          // Wait until the next task's scheduled time or until a new task is added.
          condition_.wait_until(lock, next_task_time);
        } else {
          // Task is due: pop it and execute outside the lock.
          auto scheduled_task = queue_.top();
          queue_.pop();
          lock.unlock();
          scheduled_task.task();
          lock.lock();
        }
      }
    }
  }
};

}  // namespace hela::eillance
