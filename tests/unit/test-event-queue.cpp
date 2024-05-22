#include "hela/eillance/common/event_queue.hpp"

#include <gtest/gtest.h>

namespace hela::eillance {

TEST(EventQueue, General) {
  struct MyEvent {
    int num;
  };

  std::atomic_int result = 0;

  // Create an event queue.
  hela::eillance::EventQueue event_queue;

  // Subscribe to events of type "MyEvent".
  event_queue.Subscribe<MyEvent>([&](const MyEvent& event) {
    ASSERT_EQ(event.num, 42);
    result = event.num;
  });

  ASSERT_EQ(result, 0);

  // Push an event to the queue.
  auto event{MyEvent{42}};
  event_queue.Push(event);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result, 42);
}

TEST(EventQueue, Singleton) {
  struct MyEvent {
    int num;
  };

  std::atomic_int result = 0;

  // Subscribe to events of type "MyEvent".
  EventQueue::Instance().Subscribe<MyEvent>([&](const MyEvent& event) {
    ASSERT_EQ(event.num, 42);
    result = event.num;
  });

  ASSERT_EQ(result, 0);

  // Push an event to the queue.
  auto event{MyEvent{42}};
  EventQueue::Instance().Push(event);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result, 42);
}

TEST(EventQueue, SubscribeForString) {
  bool eventHandled = false;
  auto callback = [&eventHandled](const std::string&) {
    eventHandled = true;
  };

  const auto id = EventQueue::Instance().Subscribe<std::string>(callback);
  EventQueue::Instance().Push(std::string("Test Event"));

  // Allow some time for the event to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_TRUE(eventHandled);
}

TEST(EventQueue, EventNotHandledAfterUnsubscribe) {
  bool event_handled = false;

  const auto id = EventQueue::Instance().Subscribe<std::string>([&event_handled](const std::string&) {
    event_handled = true;
  });
  EventQueue::Instance().Unsubscribe<std::string>(id);
  EventQueue::Instance().Push(std::string("Test Event"));

  // Allow some time for the event to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_FALSE(event_handled);
}

}  // namespace hela::eillance
