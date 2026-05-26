#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/transport/network_config.hpp"

using namespace eillance::transport;

TEST(NetworkConfigTest, ParseUrls) {
  std::string const input = "url1\nurl2,url3; url4\rurl5";
  auto const urls = ParseUrls(input);
  ASSERT_EQ(urls.size(), 5);
  EXPECT_EQ(urls[0], "url1");
  EXPECT_EQ(urls[1], "url2");
  EXPECT_EQ(urls[2], "url3");
  EXPECT_EQ(urls[3], "url4");
  EXPECT_EQ(urls[4], "url5");
}

TEST(NetworkConfigTest, ParseUrlsEmpty) {
  EXPECT_TRUE(ParseUrls("").empty());
  EXPECT_TRUE(ParseUrls(" \n , ; ").empty());
}

TEST(NetworkConfigTest, DefaultBootstrapNodeUrls) {
  auto const urls = GetDefaultBootstrapNodeUrls();
  EXPECT_FALSE(urls.empty());
  EXPECT_TRUE(urls.find("/dns4/") != std::string::npos);
}
