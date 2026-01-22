#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "eillance/utils/event_queue.hpp"

// Event structs for testing
struct TestEvent1 {
  int value;
};
struct TestEvent2 {
  std::string text;
};

// Helper to wait for a condition
template <typename Func>
static void WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
  auto const start = std::chrono::steady_clock::now();
  while (!condition()) {
    if (std::chrono::steady_clock::now() - start > timeout) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
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
