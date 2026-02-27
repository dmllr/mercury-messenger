#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/capture/config.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/utils/time.hpp"

using namespace eillance;

class MockSource : public Stream::Source {
  std::vector<uint64_t> timestamps_;
  size_t index_ = 0;
  Type type_;

 public:
  MockSource(Type t, std::vector<uint64_t> ts)
      : type_(t),
        timestamps_(std::move(ts)) {
  }

  void Configure(Config) override {
  }
  void Start() override {
  }
  void Stop() override {
  }

  void LoadNextSample() override {
    if (index_ < timestamps_.size()) {
      index_++;
    }
  }

  auto GetSampleTime() -> uint64_t override {
    if (index_ < timestamps_.size()) {
      return timestamps_[index_];
    }
    // Return a time slightly in the future to keep the stream loop spinning
    // but not blocking indefinitely, allowing Stop() to join the thread.
    // 50ms is enough to avoid busy loop but short enough for test teardown.
    return Time::Now() + 50000ULL;  // 50ms in microseconds
  }

  auto GetSampleDuration() -> uint64_t override {
    return 10000;
  }  // 10ms

  auto GetSample() -> std::vector<std::byte> override {
    // Return a dummy byte indicating source type for verification
    return {static_cast<std::byte>(type_ == Type::video ? 'V' : 'A')};
  }
};

class StreamTest : public ::testing::Test {
 protected:
  // Helper to wait for a specific number of samples
  auto WaitForSamples(size_t count, std::vector<std::pair<Stream::Source::Type, uint64_t>>& samples) -> bool {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::seconds(2), [&] { return samples.size() >= count; });
  }

  std::mutex mutex_;
  std::condition_variable cv_;
};

TEST_F(StreamTest, AudioVideoSynchronization) {
  auto const base_time = Time::Now();

  // Scenario:
  // Audio: 0ms, 10ms, 20ms, 30ms, 40ms
  // Video: 0ms, 20ms, 40ms
  //
  // Expected processing order (Audio < Video -> Audio; Audio >= Video -> Video):
  // 1. A(0) vs V(0) -> V(0)  [Stream priority: if A < V pick A, else V. 0 < 0 is false, pick V]
  // 2. A(0) vs V(20) -> A(0)
  // 3. A(10) vs V(20) -> A(10)
  // 4. A(20) vs V(20) -> V(20)
  // 5. A(20) vs V(40) -> A(20)
  // 6. A(30) vs V(40) -> A(30)
  // 7. A(40) vs V(40) -> V(40)
  // 8. A(40) vs V(MAX) -> A(40)

  std::vector<uint64_t> const audio_ts =
      {base_time + 0, base_time + 10000, base_time + 20000, base_time + 30000, base_time + 40000};
  std::vector<uint64_t> const video_ts = {base_time + 0, base_time + 20000, base_time + 40000};

  auto audio_source = std::make_shared<MockSource>(Stream::Source::Type::audio, audio_ts);
  auto video_source = std::make_shared<MockSource>(Stream::Source::Type::video, video_ts);

  auto stream = std::make_shared<Stream>(video_source, audio_source);

  std::vector<std::pair<Stream::Source::Type, uint64_t>> received_samples;

  auto sub = EventQueue::Instance().Subscribe<EventMediaSample>([&](auto const& e) {
    std::lock_guard<std::mutex> const lock(mutex_);
    received_samples.emplace_back(static_cast<Stream::Source::Type>(e.type), e.timestamp);
    cv_.notify_one();
  });

  stream->Start();

  // Expect 8 samples total
  ASSERT_TRUE(WaitForSamples(8, received_samples));
  stream->Stop();

  std::lock_guard<std::mutex> const lock(mutex_);
  ASSERT_EQ(received_samples.size(), 8);

  // Check Order
  // 1. V(0)
  EXPECT_EQ(received_samples[0].first, Stream::Source::Type::video);
  EXPECT_EQ(received_samples[0].second, base_time + 0);

  // 2. A(0)
  EXPECT_EQ(received_samples[1].first, Stream::Source::Type::audio);
  EXPECT_EQ(received_samples[1].second, base_time + 0);

  // 3. A(10)
  EXPECT_EQ(received_samples[2].first, Stream::Source::Type::audio);
  EXPECT_EQ(received_samples[2].second, base_time + 10000);

  // 4. V(20)
  EXPECT_EQ(received_samples[3].first, Stream::Source::Type::video);
  EXPECT_EQ(received_samples[3].second, base_time + 20000);

  // 5. A(20)
  EXPECT_EQ(received_samples[4].first, Stream::Source::Type::audio);
  EXPECT_EQ(received_samples[4].second, base_time + 20000);

  // 6. A(30)
  EXPECT_EQ(received_samples[5].first, Stream::Source::Type::audio);
  EXPECT_EQ(received_samples[5].second, base_time + 30000);

  // 7. V(40)
  EXPECT_EQ(received_samples[6].first, Stream::Source::Type::video);
  EXPECT_EQ(received_samples[6].second, base_time + 40000);

  // 8. A(40)
  EXPECT_EQ(received_samples[7].first, Stream::Source::Type::audio);
  EXPECT_EQ(received_samples[7].second, base_time + 40000);

  EventQueue::Instance().Unsubscribe<EventMediaSample>(sub);
}

TEST_F(StreamTest, StreamStopStopsDispatching) {
  auto audio_source = std::make_shared<MockSource>(
      Stream::Source::Type::audio,
      std::vector<uint64_t>{Time::Now(), Time::Now() + 10000, Time::Now() + 20000}
  );
  auto video_source = std::make_shared<MockSource>(Stream::Source::Type::video, std::vector<uint64_t>{});

  auto stream = std::make_shared<Stream>(video_source, audio_source);

  std::atomic<int> count = 0;
  auto const sub = EventQueue::Instance().Subscribe<EventMediaSample>([&](auto const&) {
    ++count;
    if (count == 1) {
      stream->Stop();
    }
  });

  stream->Start();

  // Give enough time for potentially more samples if stop failed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Should have processed the first one, then stopped
  EXPECT_EQ(count, 1);

  EventQueue::Instance().Unsubscribe<EventMediaSample>(sub);
}

TEST_F(StreamTest, DestructionWhileHandlerBusy) {
  // Scenario: Handler takes time to execute. Stream is destroyed while handler is running.
  // If variables are ordered incorrectly, this triggers "mutex destroyed while busy".
  auto audio_source = std::make_shared<MockSource>(Stream::Source::Type::audio, std::vector<uint64_t>{});
  auto video_source = std::make_shared<MockSource>(Stream::Source::Type::video, std::vector<uint64_t>{});

  auto sub = EventQueue::Instance().Subscribe<EventMediaSample>([&](auto const&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  });

  {
    auto stream = std::make_shared<Stream>(video_source, audio_source);

    stream->Start();

    // Let the stream start processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Destructor called here. Should block until the handler finishes.
  }

  EventQueue::Instance().Unsubscribe<EventMediaSample>(sub);
  // If we reach here without a crash/TSan error, the test passes.
  SUCCEED();
}
