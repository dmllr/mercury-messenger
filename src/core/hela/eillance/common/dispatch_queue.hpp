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
  explicit DispatchQueue(std::string name, std::size_t threadCount) : name(std::move(name)), threads(threadCount) {
    for (auto& thread: threads) {
      thread = std::thread(&DispatchQueue::dispatchThreadHandler, this);
    }
  }

  ~DispatchQueue() {
    // Signal to dispatch threads that it's time to wrap up
    std::unique_lock<std::mutex> lock(lockMutex);
    quit = true;
    lock.unlock();
    condition.notify_all();

    // Wait for threads to finish before we exit
    for (auto& thread: threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  static DispatchQueue& MainThread() {
    static DispatchQueue instance("Main", 1);
    return instance;
  }

  // dispatch and copy
  void dispatch(const FpT& op) {
    std::unique_lock<std::mutex> lock(lockMutex);
    queue.push(op);

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    lock.unlock();
    condition.notify_one();
  }

  // dispatch and move
  void dispatch(FpT&& op) {
    std::unique_lock<std::mutex> lock(lockMutex);
    queue.push(std::move(op));

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    lock.unlock();
    condition.notify_one();
  }

  void removePending() {
    std::unique_lock<std::mutex> const lock(lockMutex);
    queue = {};
  }

  // Deleted operations
  DispatchQueue(const DispatchQueue& rhs) = delete;
  auto operator=(const DispatchQueue& rhs) -> DispatchQueue& = delete;
  DispatchQueue(DispatchQueue&& rhs) = delete;
  auto operator=(DispatchQueue&& rhs) -> DispatchQueue& = delete;

private:
  std::string name;
  std::mutex lockMutex;
  std::vector<std::thread> threads;
  std::queue<FpT> queue;
  std::condition_variable condition;
  bool quit = false;

  void dispatchThreadHandler() {
    std::unique_lock<std::mutex> lock(lockMutex);
    while (!quit) {
      // Wait until we have data or a quit signal
      condition.wait(lock, [this] { return (!queue.empty() || quit); });

      // After wait, we own the lock
      if (!queue.empty()) {
        auto op = std::move(queue.front());
        queue.pop();

        // Release the lock before executing the operation
        lock.unlock();

        // Execute the operation
        op();

        // Reacquire the lock before looping
        lock.lock();
      }
    }
  }
};

}  // namespace hela::eillance
