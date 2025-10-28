#include "eillance/utils/event_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

// Event structs for testing
struct TestEvent1 {
  int value;
};
struct TestEvent2 {
  std::string text;
};

TEST(EventQueueTest, SubscribeAndPush) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> received_value = 0;

  auto subId = queue.Subscribe<TestEvent1>([&](const TestEvent1& event) {
    received_value = event.value;
  });

  queue.Push(TestEvent1{42});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Allow time for dispatch

  EXPECT_EQ(received_value, 42);

  queue.Unsubscribe<TestEvent1>(subId);
}

TEST(EventQueueTest, MultipleSubscribers) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> counter1 = 0;
  std::atomic<int> counter2 = 0;

  auto subId1 = queue.Subscribe<TestEvent1>([&](const TestEvent1& event) {
    counter1 += event.value;
  });
  auto subId2 = queue.Subscribe<TestEvent1>([&](const TestEvent1& event) {
    counter2 += event.value * 2;
  });

  queue.Push(TestEvent1{10});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(counter1, 10);
  EXPECT_EQ(counter2, 20);

  queue.Unsubscribe<TestEvent1>(subId1);
  queue.Unsubscribe<TestEvent1>(subId2);
}

TEST(EventQueueTest, Unsubscribe) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<bool> received = false;

  auto subId = queue.Subscribe<TestEvent1>([&](const TestEvent1& /*event*/) {
    received = true;
  });

  queue.Unsubscribe<TestEvent1>(subId);
  queue.Push(TestEvent1{100});

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_FALSE(received);
}

TEST(EventQueueTest, DifferentEventTypes) {
  auto& queue = eillance::EventQueue::Instance();
  std::atomic<int> event1_value = 0;
  std::atomic<bool> event2_received = false;

  auto subId1 = queue.Subscribe<TestEvent1>([&](const TestEvent1& event) {
    event1_value = event.value;
  });
  auto subId2 = queue.Subscribe<TestEvent2>([&](const TestEvent2& /*event*/) {
    event2_received = true;
  });

  queue.Push(TestEvent1{99});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(event1_value, 99);
  EXPECT_FALSE(event2_received);

  queue.Push(TestEvent2{"hello"});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_TRUE(event2_received);

  queue.Unsubscribe<TestEvent1>(subId1);
  queue.Unsubscribe<TestEvent2>(subId2);
}
