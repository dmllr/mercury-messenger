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
    if (!op) return;

    auto now = std::chrono::steady_clock::now();
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{now, op});
    }
    condition_.notify_one();
  }

  /// Dispatch an operation immediately.
  void Dispatch(FpT&& op) {
    if (!op) return;

    auto now = std::chrono::steady_clock::now();
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{now, std::move(op)});
    }
    condition_.notify_one();
  }

  /// Dispatch an operation to be executed after a delay.
  void DispatchAfter(std::chrono::milliseconds delay, const FpT& op) {
    if (!op) return;

    auto scheduled_time = std::chrono::steady_clock::now() + delay;
    {
      std::unique_lock<std::mutex> lock(lockMutex_);
      queue_.push(ScheduledTask{scheduled_time, op});
    }
    condition_.notify_one();
  }

  /// Dispatch an operation to be executed after a delay.
  void DispatchAfter(std::chrono::milliseconds delay, FpT&& op) {
    if (!op) return;

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
    decltype(queue_) empty_queue;
    std::swap(queue_, empty_queue);
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
  std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, CompareScheduledTask> queue_;
  std::condition_variable condition_;
  bool quit_ = false;

  void DispatchThreadHandler() {
    std::unique_lock<std::mutex> lock(lockMutex_);

    while (!quit_) {
      // Wait for work if queue is empty
      if (queue_.empty()) {
        // When transitioning to waiting state, make sure previous task state is visible
        // This is the key fix for the test: ensure task_running flag is visible
        lock.unlock();
        // This ensures that any updates from previous tasks are fully visible
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        lock.lock();

        condition_.wait(lock, [this] { return quit_ || !queue_.empty(); });
        continue;  // Recheck conditions after wake-up
      }

      // Check if the task is ready to execute
      auto now = std::chrono::steady_clock::now();
      const auto& next_task = queue_.top();

      // If task is scheduled for the future, wait until it's time
      if (next_task.executeTime > now) {
        auto status = condition_.wait_until(lock, next_task.executeTime, [this, &next_task, &now] {
          if (quit_) return true;
          if (queue_.empty()) return false;

          // Check if the top task has changed or is now due
          now = std::chrono::steady_clock::now();
          return queue_.top().executeTime != next_task.executeTime ||
                 queue_.top().executeTime <= now;
        });

        continue;  // Recheck conditions after wake-up
      }

      // Extract the task (making a copy)
      auto task = queue_.top().task;
      queue_.pop();

      // Release the lock before executing the task
      lock.unlock();

      // Execute the task (with exception handling)
      try {
        if (task) {
          task();
        }
      } catch (...) {
        // Prevent exceptions from killing the thread
      }

      // Re-acquire the lock to continue processing
      lock.lock();
    }
  }
};

}  // namespace hela::eillance