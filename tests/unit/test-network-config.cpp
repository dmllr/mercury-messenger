#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/transport/network_config.hpp"

using namespace eillance::transport;

TEST(NetworkConfigTest, ParseUrls) {
  std::string const input =
      "/dns4/node101.example.com/tcp/10807\n/dns4/node102.example.com/tcp/10807,"
      "/dns4/node103.example.com/tcp/10807; /dns4/node104.example.com/tcp/10807\r"
      "/dns4/node105.example.com/tcp/10807\n/dns4/10.2.3.4/tcp/10807";
  auto const urls = ParseUrls(input);
  ASSERT_EQ(urls.size(), 6);
  EXPECT_EQ(urls[0], "/dns4/node101.example.com/tcp/10807");
  EXPECT_EQ(urls[1], "/dns4/node102.example.com/tcp/10807");
  EXPECT_EQ(urls[2], "/dns4/node103.example.com/tcp/10807");
  EXPECT_EQ(urls[3], "/dns4/node104.example.com/tcp/10807");
  EXPECT_EQ(urls[4], "/dns4/node105.example.com/tcp/10807");
  EXPECT_EQ(urls[5], "/dns4/10.2.3.4/tcp/10807");
}

TEST(NetworkConfigTest, ParseUrlsWithErrors) {
  std::string const input =
      "/dns4/node101.example.com/tcp/10807\n/dns4/node102.example.com/tcp/10807,"
      "/dns4/node103.example.com/tcp/10807; /fake/tcp/10807;/dns4/node104.example.com/tcp/10807\r"
      "/dns4/node105.example.com/tcp/10807\n/dns4/10.2.3.4/tcp/10807\nbad-item";
  auto const urls = ParseUrls(input);
  ASSERT_EQ(urls.size(), 6);
  EXPECT_EQ(urls[0], "/dns4/node101.example.com/tcp/10807");
  EXPECT_EQ(urls[1], "/dns4/node102.example.com/tcp/10807");
  EXPECT_EQ(urls[2], "/dns4/node103.example.com/tcp/10807");
  EXPECT_EQ(urls[3], "/dns4/node104.example.com/tcp/10807");
  EXPECT_EQ(urls[4], "/dns4/node105.example.com/tcp/10807");
  EXPECT_EQ(urls[5], "/dns4/10.2.3.4/tcp/10807");
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
