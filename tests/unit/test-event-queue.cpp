#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/capture/video_config.hpp"

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
  event_queue.Push(std::move(event));

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
  EventQueue::Instance().Push(std::move(event));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result, 42);
}

TEST(EventQueue, SubscribeForString) {
  bool event_handled = false;
  auto callback = [&event_handled](const std::string&) {
    event_handled = true;
  };

  const auto id = EventQueue::Instance().Subscribe<std::string>(callback);
  EventQueue::Instance().Push(std::string("Test Event"));

  // Allow some time for the event to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_TRUE(event_handled);
}

TEST(EventQueue, DifferentTypes) {
  struct Event1 {
    int num;
  };
  struct Event2 {
    std::string str;
  };

  auto result1{Event1{0}};
  auto result2{Event2{""}};

  // Subscribe to events of type "MyEvent".
  EventQueue::Instance().Subscribe<Event1>([&](const Event1& event) {
    result1 = event;
  });
  EventQueue::Instance().Subscribe<Event2>([&](const Event2& event) {
    result2 = event;
  });

  ASSERT_EQ(result1.num, 0);
  ASSERT_EQ(result2.str, "");

  // Push an event to the queue.
  EventQueue::Instance().Push(Event1{42});
  EventQueue::Instance().Push(Event2{"valuable"});

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result1.num, 42);
  ASSERT_EQ(result2.str, "valuable");
}

TEST(EventQueue, SubscribeOnePushAnother) {
  struct Event1 {
    int num;
  };
  struct Event2 {
    std::string str;
  };

  auto result1{Event1{0}};

  // Subscribe to events of type "MyEvent".
  EventQueue::Instance().Subscribe<Event1>([&](const Event1& event) {
    result1 = event;
  });

  ASSERT_EQ(result1.num, 0);

  // Push an event to the queue.
  EventQueue::Instance().Push(Event2{"valuable"});

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result1.num, 0);
}

TEST(EventQueue, VideoConfigEvent) {
  VideoConfig result{};

  EventQueue::Instance().Subscribe<VideoConfig>([&](const VideoConfig& event) {
    result = event;
  });

  ASSERT_EQ(result.cameraId, 0);
  ASSERT_EQ(result.width, VideoConfigDefaults::width);
  ASSERT_EQ(result.height, VideoConfigDefaults::height);
  ASSERT_EQ(result.frameRate, VideoConfigDefaults::frameRate);
  ASSERT_EQ(result.bitrate, VideoConfigDefaults::bitrate);

  auto conf = VideoConfig{2,1920,1080,42,3000000};

  EventQueue::Instance().Push(std::move(conf));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result.cameraId, 2);
  ASSERT_EQ(result.width, 1920);
  ASSERT_EQ(result.height, 1080);
  ASSERT_EQ(result.frameRate, 42);
  ASSERT_EQ(result.bitrate, 3000000);
}

TEST(EventQueue, MultipleVideoConfigEvent) {
  VideoConfig result{};

  EventQueue::Instance().Subscribe<VideoConfig>([&](const VideoConfig& event) {
    result = event;
  });

  ASSERT_EQ(result.cameraId, 0);
  ASSERT_EQ(result.width, VideoConfigDefaults::width);
  ASSERT_EQ(result.height, VideoConfigDefaults::height);
  ASSERT_EQ(result.frameRate, VideoConfigDefaults::frameRate);
  ASSERT_EQ(result.bitrate, VideoConfigDefaults::bitrate);

  auto conf42 = VideoConfig{2,1920,1080,43,3000000};
  EventQueue::Instance().Push(std::move(conf42));
  auto conf43 = VideoConfig{2,1920,1080,43,3000000};
  EventQueue::Instance().Push(std::move(conf43));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  ASSERT_EQ(result.cameraId, 2);
  ASSERT_EQ(result.width, 1920);
  ASSERT_EQ(result.height, 1080);
  ASSERT_EQ(result.frameRate, 43);
  ASSERT_EQ(result.bitrate, 3000000);
}

// TODO investigate!
//TEST(EventQueue, EventNotHandledAfterUnsubscribe) {
//  bool event_handled = false;
//
//  const auto id = EventQueue::Instance().Subscribe<std::string>([&event_handled](const std::string&) {
//    event_handled = true;
//  });
//  EventQueue::Instance().Unsubscribe<std::string>(id);
//  EventQueue::Instance().Push(std::string("Test Event"));
//
//  // Allow some time for the event to be processed
//  std::this_thread::sleep_for(std::chrono::milliseconds(10));
//
//  EXPECT_FALSE(event_handled);
//}

}  // namespace hela::eillance
