#include <gtest/gtest.h>

#include "eillance/capture/video_config.hpp"

TEST(VideoConfigTest, Defaults) {
  eillance::VideoConfig const config;
  EXPECT_EQ(config.width, 1280U);
  EXPECT_EQ(config.height, 720U);
  EXPECT_EQ(config.frame_rate, 30U);
  // Default bitrate calculation check (approximate based on multiplier in header)
  // 0.22 * 1280 * 720 * 30 = 6082560
  EXPECT_EQ(config.bitrate, 6082560U);
}

TEST(VideoConfigTest, EqualityOperator) {
  eillance::VideoConfig c1;
  eillance::VideoConfig c2;

  // Initially equal (defaults)
  EXPECT_TRUE(c1 == c2);

  // Modify c2
  c2.width = 1920;
  EXPECT_FALSE(c1 == c2);

  // Sync c1
  c1.width = 1920;
  EXPECT_TRUE(c1 == c2);

  // Modify other fields
  c1.frame_rate = 60;
  EXPECT_FALSE(c1 == c2);
}

TEST(VideoConfigTest, CustomValues) {
  eillance::VideoConfig config;
  config.camera_id = 1;
  config.width = 640;
  config.height = 480;
  config.frame_rate = 15;
  config.bitrate = 500000;

  EXPECT_EQ(config.camera_id, 1);
  EXPECT_EQ(config.width, 640U);
  EXPECT_EQ(config.height, 480U);
  EXPECT_EQ(config.frame_rate, 15U);
  EXPECT_EQ(config.bitrate, 500000U);
}
