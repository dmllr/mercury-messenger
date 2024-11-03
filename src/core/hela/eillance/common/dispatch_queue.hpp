#pragma once

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

public:
  explicit DispatchQueue(std::string name, std::size_t threadCount) : name_(std::move(name)), threads_(threadCount) {
    for (auto& thread: threads_) {
      thread = std::thread(&DispatchQueue::DispatchThreadHandler, this);
    }
  }

  ~DispatchQueue() {
    std::unique_lock<std::mutex> lock(lockMutex_);
    quit_ = true;
    lock.unlock();
    condition_.notify_all();

    for (auto& thread: threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  static DispatchQueue& MainThread() {
    static DispatchQueue instance("Main", 1);
    return instance;
  }

  void Dispatch(const FpT& op) {
    std::unique_lock<std::mutex> lock(lockMutex_);
    queue_.push(op);
    lock.unlock();
    condition_.notify_one();
  }

  void Dispatch(FpT&& op) {
    std::unique_lock<std::mutex> lock(lockMutex_);
    queue_.push(std::move(op));
    lock.unlock();
    condition_.notify_one();
  }

  void DispatchAfter(std::chrono::milliseconds delay, FpT op) {
    auto delayed_op = [delay, op = std::move(op)]() {
      std::this_thread::sleep_for(delay);
      op();
    };
    Dispatch(std::move(delayed_op));
  }

  void RemovePending() {
    std::unique_lock<std::mutex> const lock(lockMutex_);
    queue_ = {};
  }

  DispatchQueue(const DispatchQueue& rhs) = delete;
  auto operator=(const DispatchQueue& rhs) -> DispatchQueue& = delete;
  DispatchQueue(DispatchQueue&& rhs) = delete;
  auto operator=(DispatchQueue&& rhs) -> DispatchQueue& = delete;

private:
  std::string name_;
  std::mutex lockMutex_;
  std::vector<std::thread> threads_;
  std::queue<FpT> queue_;
  std::condition_variable condition_;
  bool quit_ = false;

  void DispatchThreadHandler() {
    std::unique_lock<std::mutex> lock(lockMutex_);
    while (!quit_) {
      condition_.wait(lock, [this] { return (!queue_.empty() || quit_); });

      if (!queue_.empty()) {
        auto op = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        op();
        lock.lock();
      }
    }
  }
};

}  // namespace hela::eillance
