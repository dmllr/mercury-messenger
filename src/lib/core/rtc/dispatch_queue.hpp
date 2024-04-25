#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <string>
#include <vector>

class DispatchQueue {
    using fp_t = std::function<void()>;

public:
    explicit DispatchQueue(std::string name, size_t threadCount = 1) : name(std::move(name)), threads(threadCount) {
        for (auto& i: threads) {
            i = std::thread(&DispatchQueue::dispatchThreadHandler, this);
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
    void dispatch(const fp_t& op) {
        std::unique_lock<std::mutex> lock(lockMutex);
        queue.push(op);

        // Manual unlocking is done before notifying, to avoid waking up
        // the waiting thread only to block again (see notify_one for details)
        lock.unlock();
        condition.notify_one();
    }

    // dispatch and move
    void dispatch(fp_t&& op) {
        std::unique_lock<std::mutex> lock(lockMutex);
        queue.push(std::move(op));

        // Manual unlocking is done before notifying, to avoid waking up
        // the waiting thread only to block again (see notify_one for details)
        lock.unlock();
        condition.notify_one();
    }

    void removePending() {
        std::unique_lock<std::mutex> lock(lockMutex);
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
    std::queue<fp_t> queue;
    std::condition_variable condition;
    bool quit = false;

    void dispatchThreadHandler() {
        std::unique_lock<std::mutex> lock(lockMutex);
        do {
            //Wait until we have data or a quit signal
            condition.wait(lock, [this] {
                return (!queue.empty() || quit);
            });

            //after wait, we own the lock
            if (!quit && !queue.empty()) {
                auto op = std::move(queue.front());
                queue.pop();

                //unlock now that we're done messing with the queue
                lock.unlock();

                op();

                lock.lock();
            }
        } while (!quit);
    }
};
