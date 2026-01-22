#include <gtest/gtest.h>

#include "eillance/capture/capture.hpp"
#include "eillance/rtc/stream_source.hpp"

using namespace eillance;

namespace {  // Anonymous namespace to avoid ODR violations with other tests

// Mock implementation of CaptureInterface concept
class MockCaptureImpl {
 public:
  bool configured = false;
  bool engaged = false;

  void Configure(Config) { configured = true; }
  void Engage() { engaged = true; }
  void Revoke() { engaged = false; }

  auto FetchBuffers() -> Packet {
    std::vector<std::byte> data = {std::byte{0xAA}, std::byte{0xBB}};
    return Packet(data);
  }

  auto GetSampleDuration() -> Time::Unit { return 33000; }  // 33ms
};

}  // namespace

TEST(StreamSourceTest, Lifecycle) {
  auto mock = std::make_unique<MockCaptureImpl>();
  auto* raw_mock = mock.get();

  Capture capture(std::move(mock));
  StreamSource source(std::move(capture));

  // Configure
  source.Configure({});
  EXPECT_TRUE(raw_mock->configured);

  // Start
  source.Start();
  EXPECT_TRUE(raw_mock->engaged);

  // Stop
  source.Stop();
  EXPECT_FALSE(raw_mock->engaged);
}

TEST(StreamSourceTest, SampleProcessing) {
  auto mock = std::make_unique<MockCaptureImpl>();
  Capture capture(std::move(mock));
  StreamSource source(std::move(capture));

  // Must start to engage capture
  source.Start();

  source.LoadNextSample();

  auto sample = source.GetSample();
  ASSERT_EQ(sample.size(), 2);
  EXPECT_EQ(sample[0], std::byte{0xAA});
  EXPECT_EQ(sample[1], std::byte{0xBB});

  EXPECT_GT(source.GetSampleTime(), 0);
  EXPECT_EQ(source.GetSampleDuration(), 33000);
}
