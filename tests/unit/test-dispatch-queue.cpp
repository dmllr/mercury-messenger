#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance {

class DispatchQueueTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Give time for any previous tests to clean up
    eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));
  }

  void TearDown() override {
    // Give time for any pending operations to complete
    eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));
  }
};

TEST_F(DispatchQueueTest, DispatchAndExecute) {
  DispatchQueue queue("TestQueue", 1);

  std::atomic<int> counter{0};
  queue.Dispatch([&counter]() { counter++; });

  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == 1; }, std::chrono::milliseconds(100)));
}

TEST_F(DispatchQueueTest, DispatchAndMove) {
  DispatchQueue queue("TestQueue", 1);

  std::atomic<int> counter{0};
  queue.Dispatch(std::move([&counter]() { counter++; }));

  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == 1; }, std::chrono::milliseconds(100)));
}

TEST_F(DispatchQueueTest, RemovePending) {
  DispatchQueue queue("TestQueue", 1);

  std::atomic<int> counter{0};

  // Add a short delay to ensure the task gets queued but not executed immediately
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(10));

  // Dispatch with slight delay to ensure it's still in the queue when we remove
  queue.DispatchAfter(std::chrono::milliseconds(50), [&counter]() { counter++; });

  // Remove the pending operation before it executes
  queue.RemovePending();

  // Wait to ensure the operation had time to execute if it wasn't removed
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));

  EXPECT_EQ(counter, 0);
}

TEST_F(DispatchQueueTest, MainThread) {
  std::atomic<int> counter{0};
  DispatchQueue::MainThread().Dispatch([&counter]() { counter++; });

  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == 1; }, std::chrono::milliseconds(100)));
}

TEST_F(DispatchQueueTest, MultipleThreads) {
  size_t const num_threads = 4;
  DispatchQueue queue("MultiThreadQueue", num_threads);

  std::atomic<int> counter{0};
  int const num_tasks = 100;

  for (int i = 0; i < num_tasks; i++) {
    queue.Dispatch([&counter]() {
      counter++;
      // Add a small sleep to increase thread contention
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
  }

  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == num_tasks; }, std::chrono::milliseconds(1000)));
}

TEST_F(DispatchQueueTest, ScheduledExecution) {
  DispatchQueue queue("ScheduledQueue", 1);

  std::atomic<int> counter{0};
  auto start = std::chrono::steady_clock::now();

  // Schedule task with delay
  auto const delay = std::chrono::milliseconds(100);
  queue.DispatchAfter(delay, [&counter]() { counter++; });

  // Wait for task execution
  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == 1; }, std::chrono::milliseconds(200)));

  auto elapsed = std::chrono::steady_clock::now() - start;
  // Check if the execution happened after the delay (with some tolerance)
  EXPECT_GE(elapsed, delay - std::chrono::milliseconds(10));
}

TEST_F(DispatchQueueTest, PriorityOrdering) {
  DispatchQueue queue("PriorityQueue", 1);

  std::mutex mutex;
  std::vector<int> execution_order;

  // Schedule tasks in reverse priority order
  for (int i = 5; i >= 1; i--) {
    queue.DispatchAfter(std::chrono::milliseconds(i * 20), [i, &mutex, &execution_order]() {
      std::lock_guard<std::mutex> const lock(mutex);
      execution_order.push_back(i);
    });
  }

  // Wait for all tasks to complete
  eillance::EventQueue::Instance().WaitFor([&] { return execution_order.size() == 5; }, std::chrono::milliseconds(200));

  // Verify execution order
  std::vector<int> const expected_order = {1, 2, 3, 4, 5};
  EXPECT_EQ(execution_order, expected_order);
}

TEST_F(DispatchQueueTest, TasksExecutedOnce) {
  DispatchQueue queue("OnceQueue", 1);

  std::atomic<int> counter{0};
  std::atomic<bool> task_running{false};

  // Create a task that runs for a while
  auto task = [&]() {
    if (task_running.exchange(true)) {
      // Task is already running, this is a duplicate execution
      FAIL() << "Task executed more than once concurrently";
    }

    counter++;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    task_running = false;
  };

  // Dispatch the task multiple times
  for (int i = 0; i < 10; i++) {
    queue.Dispatch(task);
  }

  EXPECT_TRUE(
      EventQueue::Instance()
          .WaitFor([&]() { return counter == 10 && !task_running.load(); }, std::chrono::milliseconds(1000))
  );
  EXPECT_FALSE(task_running);
}

TEST_F(DispatchQueueTest, ExceptionsHandling) {
  DispatchQueue queue("ExceptionQueue", 1);

  std::atomic<int> counter{0};

  // Task that throws an exception
  queue.Dispatch([&counter]() {
    counter++;
    throw std::runtime_error("Test exception");
  });

  // Task that should still execute after the exception
  queue.Dispatch([&counter]() { counter++; });

  // Wait for both tasks to complete
  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == 2; }, std::chrono::milliseconds(500)));
}

TEST_F(DispatchQueueTest, MultipleDelayedTasks) {
  DispatchQueue queue("DelayedQueue", 1);

  std::atomic<int> counter{0};
  int const num_tasks = 20;
  auto start = std::chrono::steady_clock::now();

  // Dispatch tasks with various delays
  for (int i = 0; i < num_tasks; i++) {
    // Random delays between 10-50ms, but out of order
    auto delay = std::chrono::milliseconds((i * 17) % 40 + 10);
    queue.DispatchAfter(delay, [&counter, i, delay]() { counter++; });
  }

  // Wait for all tasks to complete
  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == num_tasks; }, std::chrono::milliseconds(500)));
}

TEST_F(DispatchQueueTest, ConcurrentModification) {
  DispatchQueue queue("ConcurrentQueue", 2);

  std::atomic<int> counter{0};
  std::atomic<int> tasks_added{0};

  // Initial task that adds more tasks
  queue.Dispatch([&]() {
    for (int i = 0; i < 10; i++) {
      queue.Dispatch([&]() {
        counter++;
        tasks_added++;
      });
    }
  });

  // At the same time, dispatch additional tasks directly
  for (int i = 0; i < 10; i++) {
    queue.Dispatch([&]() { counter++; });
  }

  // Wait for both directly added and dynamically added tasks
  EXPECT_TRUE(
      EventQueue::Instance()
          .WaitFor([&]() { return counter == 20 && tasks_added == 10; }, std::chrono::milliseconds(500))
  );
}

TEST_F(DispatchQueueTest, DestructorCleanup) {
  std::atomic<int> counter{0};

  {
    // Create a queue that will be destroyed
    DispatchQueue queue("DestructorQueue", 1);

    // Add a task with a delay longer than the queue's lifetime
    queue.DispatchAfter(std::chrono::milliseconds(500), [&counter]() { counter++; });

    // Queue will be destroyed here
  }

  // Task should not execute after queue destruction
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));
  EXPECT_EQ(counter, 0);
}

TEST_F(DispatchQueueTest, HighConcurrencyStress) {
  size_t const num_threads = 8;
  DispatchQueue queue("StressQueue", num_threads);

  std::atomic<int> counter{0};
  int const num_tasks = 1000;

  for (int i = 0; i < num_tasks; i++) {
    queue.Dispatch([&counter]() {
      counter++;
      // Simulate varied workloads
      if (counter % 10 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }

  EXPECT_TRUE(EventQueue::Instance().WaitFor([&]() { return counter == num_tasks; }, std::chrono::milliseconds(5000)));
}

TEST_F(DispatchQueueTest, EmptyFunctionHandling) {
  DispatchQueue queue("EmptyQueue", 1);

  // Dispatch an empty function (should not crash)
  queue.Dispatch(std::function<void()>());

  // Wait to ensure no crashes occur
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  // Test passed if no crash occurred
  SUCCEED();
}

TEST_F(DispatchQueueTest, NestedDispatch) {
  DispatchQueue queue("NestedQueue", 1);

  std::atomic<int> counter{0};
  std::mutex order_mutex;
  std::vector<int> execution_order;

  queue.Dispatch([&]() {
    {
      std::lock_guard<std::mutex> const lock(order_mutex);
      execution_order.push_back(1);
    }
    counter++;

    // Nested dispatch
    queue.Dispatch([&]() {
      {
        std::lock_guard<std::mutex> const lock(order_mutex);
        execution_order.push_back(2);
      }
      counter++;

      // Double-nested dispatch
      queue.Dispatch([&]() {
        {
          std::lock_guard<std::mutex> const lock(order_mutex);
          execution_order.push_back(3);
        }
        counter++;
      });
    });
  });

  // Wait for all nested dispatches to complete
  EXPECT_TRUE(EventQueue::Instance().WaitFor([&counter]() { return counter == 3; }, std::chrono::milliseconds(500)));

  // Verify execution order
  std::vector<int> const expected = {1, 2, 3};
  EXPECT_EQ(execution_order, expected);
}

}  // namespace eillance
