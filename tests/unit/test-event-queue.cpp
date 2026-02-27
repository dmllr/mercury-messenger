#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/utils/event_queue.hpp"

// Event structs for testing
struct TestEvent1 {
  int value;
};
struct TestEvent2 {
  std::string text;
};

struct LoadEvent {
  std::int32_t id;
};

struct ExceptionEvent {
  std::int32_t value;
};

// Helper to wait for a condition
template <typename Func>
static void WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  eillance::EventQueue::Instance().WaitFor(condition, timeout);
}

TEST(EventQueueTest, SubscribeAndPush) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> received_value = 0;

  auto sub_id = queue.Subscribe<TestEvent1>([&](TestEvent1 const& event) { received_value = event.value; });

  queue.Push(TestEvent1{42});

  WaitFor([&] { return received_value == 42; });

  EXPECT_EQ(received_value, 42);

  queue.Unsubscribe<TestEvent1>(sub_id);
}

TEST(EventQueueTest, MultipleSubscribers) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> counter1 = 0;
  std::atomic<int> counter2 = 0;

  auto sub_id1 = queue.Subscribe<TestEvent1>([&](TestEvent1 const& event) { counter1 += event.value; });
  auto sub_id2 = queue.Subscribe<TestEvent1>([&](TestEvent1 const& event) { counter2 += event.value * 2; });

  queue.Push(TestEvent1{10});

  WaitFor([&] { return counter1 == 10 && counter2 == 20; });

  EXPECT_EQ(counter1, 10);
  EXPECT_EQ(counter2, 20);

  queue.Unsubscribe<TestEvent1>(sub_id1);
  queue.Unsubscribe<TestEvent1>(sub_id2);
}

TEST(EventQueueTest, Unsubscribe) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<bool> received = false;

  auto sub_id = queue.Subscribe<TestEvent1>([&](TestEvent1 const& /*event*/) { received = true; });

  queue.Unsubscribe<TestEvent1>(sub_id);
  queue.Push(TestEvent1{100});

  // Wait a bit to ensure it *doesn't* receive
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_FALSE(received.load());
}

TEST(EventQueueTest, DifferentEventTypes) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> event1_value = 0;
  std::atomic<bool> event2_received = false;

  auto sub_id1 = queue.Subscribe<TestEvent1>([&](TestEvent1 const& event) { event1_value = event.value; });
  auto sub_id2 = queue.Subscribe<TestEvent2>([&](TestEvent2 const& /*event*/) { event2_received = true; });

  queue.Push(TestEvent1{99});
  WaitFor([&] { return event1_value == 99; });
  EXPECT_EQ(event1_value, 99);
  EXPECT_FALSE(event2_received.load());

  queue.Push(TestEvent2{"hello"});
  WaitFor([&] { return event2_received.load(); });
  EXPECT_TRUE(event2_received.load());

  queue.Unsubscribe<TestEvent1>(sub_id1);
  queue.Unsubscribe<TestEvent2>(sub_id2);
}

// --- EDGE CASES ---

TEST(EventQueueTest, EdgeCases_UnsubscribeInvalid) {
  eillance::EventQueue queue;
  // Attempting to unsubscribe an ID that doesn't exist should be a safe no-op.
  queue.Unsubscribe<TestEvent1>(999999);
  SUCCEED();
}

TEST(EventQueueTest, EdgeCases_PushWithNoSubscribers) {
  eillance::EventQueue queue;
  // Pushing without any active subscribers should be successfully discarded.
  queue.Push(TestEvent1{10});
  SUCCEED();
}

TEST(EventQueueTest, EdgeCases_ExceptionInCallbackDoesNotCrash) {
  eillance::EventQueue queue;
  std::atomic<std::int32_t> process_count{0};

  queue.Subscribe<ExceptionEvent>([](ExceptionEvent const&) -> void {
    throw std::runtime_error("Simulated callback exception");
  });

  queue.Subscribe<ExceptionEvent>([&](ExceptionEvent const&) -> void {
    process_count.fetch_add(1, std::memory_order_relaxed);
  });

  queue.Push(ExceptionEvent{1});
  queue.Push(ExceptionEvent{2});

  bool const success = queue.WaitFor(
      [&]() -> bool { return process_count.load(std::memory_order_relaxed) == 2; },
      std::chrono::seconds(3)
  );

  EXPECT_TRUE(success);
  EXPECT_EQ(process_count.load(std::memory_order_relaxed), 2);
}

// --- THREAD SAFETY & SYNCHRONIZATION ---

TEST(EventQueueTest, ThreadSafety_ConcurrentSubscribeUnsubscribe) {
  eillance::EventQueue queue;
  std::atomic<bool> stop{false};
  std::atomic<std::int32_t> processed_events{0};

  auto pusher = std::thread([&]() -> void {
    std::int32_t counter = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      queue.Push(LoadEvent{counter++});
      std::this_thread::yield();
    }
  });

  auto toggler = std::thread([&]() -> void {
    constexpr std::int32_t iterations = 1000;
    for (std::int32_t i = 0; i < iterations; ++i) {
      auto id = queue.Subscribe<LoadEvent>([&](LoadEvent const&) -> void {
        processed_events.fetch_add(1, std::memory_order_relaxed);
      });
      std::this_thread::yield();
      queue.Unsubscribe<LoadEvent>(id);
    }
    stop.store(true, std::memory_order_relaxed);
  });

  toggler.join();
  pusher.join();

  // If threads join without deadlock or memory corruption, synchronization holds.
  SUCCEED();
}

// --- ALL-IN MUST-OUT GUARANTEE ---

TEST(EventQueueTest, Guarantee_AllInMustOut) {
  eillance::EventQueue queue;
  constexpr std::int32_t kEventCount = 10000;
  std::vector<std::int32_t> received_ids;
  std::mutex vec_mutex;

  queue.Subscribe<LoadEvent>([&](LoadEvent const& event) -> void {
    std::lock_guard<std::mutex> const lock(vec_mutex);
    received_ids.push_back(event.id);
  });

  for (std::int32_t i = 0; i < kEventCount; ++i) {
    queue.Push(LoadEvent{i});
  }

  bool const success = queue.WaitFor(
      [&]() -> bool {
        std::lock_guard<std::mutex> const lock(vec_mutex);
        return received_ids.size() == static_cast<std::size_t>(kEventCount);
      },
      std::chrono::seconds(10)
  );

  EXPECT_TRUE(success);

  std::lock_guard<std::mutex> const lock(vec_mutex);
  EXPECT_EQ(received_ids.size(), static_cast<std::size_t>(kEventCount));

  // Verify single-threaded dispatch ordering matches push ordering exactly, 0 missing.
  for (std::int32_t i = 0; i < kEventCount; ++i) {
    EXPECT_EQ(received_ids[static_cast<std::size_t>(i)], i);
  }
}

// --- LOAD TESTING FIXTURE ---

class EventQueueLoadTest : public ::testing::Test {
 protected:
  static void RunSingleThreadLoadTest(std::int32_t event_count) {
    eillance::EventQueue queue;
    std::atomic<std::int32_t> received_count{0};

    queue.Subscribe<LoadEvent>([&](LoadEvent const&) -> void {
      received_count.fetch_add(1, std::memory_order_relaxed);
    });

    for (std::int32_t i = 0; i < event_count; ++i) {
      queue.Push(LoadEvent{i});
    }

    EXPECT_TRUE(queue.WaitFor(
        [&]() -> bool { return received_count.load(std::memory_order_relaxed) == event_count; },
        std::chrono::seconds(15)
    ));
    EXPECT_EQ(received_count.load(std::memory_order_relaxed), event_count);
  }

  static void RunMultiThreadLoadTest(std::int32_t event_count_per_thread, std::int32_t thread_count) {
    eillance::EventQueue queue;
    std::atomic<std::int32_t> received_count{0};
    std::int32_t const total_events = event_count_per_thread * thread_count;

    queue.Subscribe<LoadEvent>([&](LoadEvent const&) -> void {
      received_count.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(thread_count));
    for (std::int32_t t = 0; t < thread_count; ++t) {
      threads.emplace_back([&queue, event_count_per_thread]() -> void {
        for (std::int32_t i = 0; i < event_count_per_thread; ++i) {
          queue.Push(LoadEvent{i});
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }

    EXPECT_TRUE(queue.WaitFor(
        [&]() -> bool { return received_count.load(std::memory_order_relaxed) == total_events; },
        std::chrono::seconds(30)
    ));
    EXPECT_EQ(received_count.load(std::memory_order_relaxed), total_events);
  }
};

TEST_F(EventQueueLoadTest, SingleThread_100) {
  RunSingleThreadLoadTest(100);
}
TEST_F(EventQueueLoadTest, SingleThread_1K) {
  RunSingleThreadLoadTest(1000);
}
TEST_F(EventQueueLoadTest, SingleThread_10K) {
  RunSingleThreadLoadTest(10000);
}
TEST_F(EventQueueLoadTest, SingleThread_100K) {
  RunSingleThreadLoadTest(100000);
}

TEST_F(EventQueueLoadTest, MultiThread_100) {
  RunMultiThreadLoadTest(25, 4);
}
TEST_F(EventQueueLoadTest, MultiThread_1K) {
  RunMultiThreadLoadTest(250, 4);
}
TEST_F(EventQueueLoadTest, MultiThread_10K) {
  RunMultiThreadLoadTest(2500, 4);
}
TEST_F(EventQueueLoadTest, MultiThread_100K) {
  RunMultiThreadLoadTest(25000, 4);
}

// --- SMOKE TEST (Max CPUs) ---

TEST_F(EventQueueLoadTest, DISABLED_SmokeTest_MaxLoadMaxThreads) {
  std::uint32_t const cpus = std::thread::hardware_concurrency();
  std::uint32_t const thread_count = 4 * cpus;
  ASSERT_GT(thread_count, 0);
  constexpr std::int32_t events_per_thread = 500000;

  RunMultiThreadLoadTest(events_per_thread, static_cast<std::int32_t>(thread_count));
}
