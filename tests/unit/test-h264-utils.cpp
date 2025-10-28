#include "eillance/utils/h264.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Helper: Read the entire file into a vector of bytes.
std::vector<std::byte> LoadFile(const std::string& filepath) {
  std::ifstream file(filepath, std::ios::binary);
  if (!file) {
    std::ostringstream err;
    err << "Cannot open file: " << filepath;
    throw std::runtime_error(err.str());
  }
  std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<std::byte> binary_buffer(buffer.size());
  std::transform(buffer.begin(), buffer.end(), binary_buffer.begin(), [](char c) {
    return std::byte{static_cast<unsigned char>(c)};
  });
  return binary_buffer;
}

}  // namespace

// Test fixture for H264Parser tests.
// NOLINTBEGIN(*-non-private-member-variables-in-classes)
class H264ParserTest : public ::testing::Test {
protected:
  const std::string frame001_path = "files/3frames/001.h264";  // I-frame (IDR)
  const std::string frame002_path = "files/3frames/002.h264";  // P/B frame
  const std::string frame003_path = "files/3frames/003.h264";  // P/B frame

  // Callback log to record callback invocations.
  std::string callbackLog_;

  // Helper: append a message to the callback log.
  void log(const std::string& message) { callbackLog_ += message + "\n"; }

  // Dummy SPS and PPS data.
  // These are minimal examples (starting with a valid start code) so that ParseSps() runs.
  const std::vector<std::byte> dummy_sps = {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
                                            std::byte{0x67},  // SPS NAL header (nal type 7)
                                            std::byte{0x42}, std::byte{0x00}, std::byte{0x1F}, std::byte{0xAC},
                                            std::byte{0x56}, std::byte{0x80}, std::byte{0x50}, std::byte{0x05},
                                            std::byte{0xB9}};

  const std::vector<std::byte> dummy_pps = {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
                                            std::byte{0x68},  // PPS NAL header (nal type 8)
                                            std::byte{0xCE}, std::byte{0x06}, std::byte{0xE2}};
};
// NOLINTEND(*-non-private-member-variables-in-classes)

//
// When the parser is unconfigured, the first (001.h264) frame containing
// multiple NALUs should trigger input/output callbacks.
//
TEST_F(H264ParserTest, FirstFrameLeadsToConfiguration) {
  eillance::H264Parser parser;

  // Set callbacks that log their invocation.
  parser.SetOnConfigureCallback([&](const eillance::VideoConfig& conf) {
    log("onConfigure");
    ASSERT_EQ(conf.width, 1280);
    ASSERT_EQ(conf.height, 720);
  });

  // Load an I-frame (001.h264) which (for example) contains multiple NALUs.
  auto i_frame_data = LoadFile(frame001_path);
  parser.ProcessFrame(i_frame_data);

  EXPECT_NE(callbackLog_.find("onConfigure"), std::string::npos);
}

//
// When the parser is unconfigured, P or B frames (00[2,3].h264)
// should not trigger input/output callbacks.
//
TEST_F(H264ParserTest, PBFramesDoNotTriggerConfigure) {
  eillance::H264Parser parser;

  // Set callbacks that log their invocation.
  parser.SetOnConfigureCallback([&](const eillance::VideoConfig&) {
    log("onConfigure");
  });

  // Load the second (002.h264) frame
  parser.ProcessFrame(LoadFile(frame002_path));
  // Load the third (003.h264) frame
  parser.ProcessFrame(LoadFile(frame003_path));

  EXPECT_EQ(callbackLog_.find("onConfigure"), std::string::npos);
}

//
// When the parser is configured (simulate configuration by first
// feeding dummy SPS and PPS), then processing frames (I-frame and subsequent
// P/B frames) should invoke the input/output callbacks.
// This test also validates that the parser correctly handles multiple NALUs
// within a single input buffer.
//
TEST_F(H264ParserTest, ConfiguredFrameProcessing) {  // NOLINT(*-function-cognitive-complexity)
  eillance::H264Parser parser;

  // Set up counters to check callback invocations.
  int configure_count = 0;

  parser.SetOnConfigureCallback([&](const eillance::VideoConfig&) {
    log("onConfigure");
    ++configure_count;
  });

  // Simulate configuration by processing dummy SPS and PPS.
  parser.ProcessFrame(dummy_sps);
  parser.ProcessFrame(dummy_pps);
  EXPECT_EQ(configure_count, 1) << "onConfigure callback should have been invoked";

  // Now feed two P/B frames.
  parser.ProcessFrame(LoadFile(frame002_path));
  parser.ProcessFrame(LoadFile(frame003_path));

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
  parser.SetOnConfigureCallback([&](const eillance::VideoConfig&) {
    log("onConfigure");
  });

  // Create a binary buffer that does not contain a valid H.264 start code.
  std::vector<std::byte> invalid_data = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};  // NOLINT(*-magic-numbers)
  parser.ProcessFrame(invalid_data);

  EXPECT_EQ(callbackLog_.find("onConfigure"), std::string::npos);
}
