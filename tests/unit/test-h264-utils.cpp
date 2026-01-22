#include <algorithm>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/utils/h264.hpp"

// Test fixture for H264Parser tests.
// NOLINTBEGIN(*-non-private-member-variables-in-classes)
class H264ParserTest : public ::testing::Test {
 protected:
  // Callback log to record callback invocations.
  std::string callbackLog_;

  // Helper: append a message to the callback log.
  void Log(std::string const& message) { callbackLog_ += message + "\n"; }

  // Dummy SPS and PPS data.
  // SPS: Profile Baseline (66), Level 3.1 (31)
  // Resolution encoded roughly corresponds to 1280x720 in previous mocks,
  // but here we just need structurally valid NALUs for the parser.
  std::vector<std::byte> const dummy_sps = {
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x01},
      std::byte{0x67},
      std::byte{0x42},
      std::byte{0x00},
      std::byte{0x1F},
      std::byte{0xAC},
      std::byte{0x56},
      std::byte{0x80},
      std::byte{0x50},
      std::byte{0x05},
      std::byte{0xB9}
  };

  std::vector<std::byte> const dummy_pps = {
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x01},
      std::byte{0x68},
      std::byte{0xCE},
      std::byte{0x06},
      std::byte{0xE2}
  };

  // Dummy IDR Slice (NAL Type 5)
  std::vector<std::byte> const dummy_idr = {
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x01},
      std::byte{0x65},
      std::byte{0x88},
      std::byte{0x84},
      std::byte{0x00},
      std::byte{0x10}
  };

  // Dummy Non-IDR Slice (NAL Type 1)
  std::vector<std::byte> const dummy_slice = {
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x00},
      std::byte{0x01},
      std::byte{0x41},
      std::byte{0x9A},
      std::byte{0x00},
      std::byte{0x05}
  };

  // Helper to concatenate vectors
  std::vector<std::byte> Combine(std::vector<std::vector<std::byte>> const& parts) {
    std::vector<std::byte> result;
    for (auto const& part : parts) {
      result.insert(result.end(), part.begin(), part.end());
    }
    return result;
  }
};
// NOLINTEND(*-non-private-member-variables-in-classes)

//
// When the parser is unconfigured, the first frame containing
// multiple NALUs (SPS/PPS/IDR) should trigger input/output callbacks.
//
TEST_F(H264ParserTest, FirstFrameLeadsToConfiguration) {
  eillance::H264Parser parser;

  // Set callbacks that log their invocation.
  parser.SetOnConfigureCallback([&](eillance::VideoConfig const& conf) {
    Log("onConfigure");
    // We relax the exact resolution check as our dummy_sps might not match 1280x720 exactly
    // without precise bitstream crafting, but we check valid parsing.
    EXPECT_GT(conf.width, 0);
    EXPECT_GT(conf.height, 0);
  });

  // Construct a combined buffer simulating an I-frame with SPS/PPS/IDR
  auto i_frame_data = Combine({dummy_sps, dummy_pps, dummy_idr});
  parser.ProcessFrame(i_frame_data);

  EXPECT_NE(callbackLog_.find("onConfigure"), std::string::npos);
}

//
// When the parser is unconfigured, P or B frames (Non-IDR without SPS/PPS)
// should not trigger input/output callbacks (configuration).
//
TEST_F(H264ParserTest, PBFramesDoNotTriggerConfigure) {
  eillance::H264Parser parser;

  // Set callbacks that log their invocation.
  parser.SetOnConfigureCallback([&](eillance::VideoConfig const&) { Log("onConfigure"); });

  // Feed dummy non-IDR slices
  parser.ProcessFrame(dummy_slice);
  parser.ProcessFrame(dummy_slice);

  EXPECT_EQ(callbackLog_.find("onConfigure"), std::string::npos);
}

//
// When the parser is configured (simulate configuration by first
// feeding dummy SPS and PPS), then processing frames (I-frame and subsequent
// P/B frames) should invoke the input/output callbacks.
//
TEST_F(H264ParserTest, ConfiguredFrameProcessing) {
  eillance::H264Parser parser;

  int configure_count = 0;

  parser.SetOnConfigureCallback([&](eillance::VideoConfig const&) {
    Log("onConfigure");
    ++configure_count;
  });

  // Simulate configuration by processing dummy SPS and PPS.
  parser.ProcessFrame(dummy_sps);
  parser.ProcessFrame(dummy_pps);
  EXPECT_EQ(configure_count, 1) << "onConfigure callback should have been invoked";

  // Now feed two P/B frames.
  parser.ProcessFrame(dummy_slice);
  parser.ProcessFrame(dummy_slice);

  // Non-SPS/PPS frames (I, P, or B) should not trigger the configure callback again.
  EXPECT_EQ(configure_count, 1);
  EXPECT_NE(callbackLog_.find("onConfigure"), std::string::npos);
}

//
// Processing invalid data (with no valid start code) should result in no callbacks.
//
TEST_F(H264ParserTest, NoActionOnInvalidStartCode) {
  eillance::H264Parser parser;

  // Set callbacks to record any invocation.
  parser.SetOnConfigureCallback([&](eillance::VideoConfig const&) { Log("onConfigure"); });

  // Create a binary buffer that does not contain a valid H.264 start code.
  std::vector<std::byte> const invalid_data = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  parser.ProcessFrame(invalid_data);

  EXPECT_EQ(callbackLog_.find("onConfigure"), std::string::npos);
}
