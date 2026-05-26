#include <memory>

#include <gtest/gtest.h>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"

class DummyCaptureImpl {
 public:
  void Configure(eillance::Config const&) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> eillance::Packet {
    return eillance::Packet();
  }
  auto GetSampleDuration() -> Time::Unit {
    return 42ULL;
  }
};

TEST(CaptureTest, TypeErasureDelegation) {
  eillance::Capture capture(std::make_unique<DummyCaptureImpl>());

  eillance::Config const config;
  capture.Configure(config);
  capture.Engage();

  auto const pkt = capture.FetchBuffers();
  EXPECT_TRUE(pkt.empty());
  EXPECT_EQ(capture.GetSampleDuration(), 42ULL);

  capture.Revoke();
}
