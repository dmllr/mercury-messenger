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
  event_queue.Subscribe<MyEvent>([&](const std::any& event) {
    auto my_event = std::any_cast<MyEvent>(event);
    ASSERT_EQ(my_event.num, 42);
    result = my_event.num;
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
  EventQueue::Instance().Subscribe<MyEvent>([&](const std::any& event) {
    auto my_event = std::any_cast<MyEvent>(event);
    ASSERT_EQ(my_event.num, 42);
    result = my_event.num;
  });

  ASSERT_EQ(result, 0);

  // Push an event to the queue.
  auto event{MyEvent{42}};
  EventQueue::Instance().Push(event);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result, 42);
}

}  // namespace hela::eillance
