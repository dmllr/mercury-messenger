#include "eillance/utils/args_parser.hpp"

#include <gtest/gtest.h>

TEST(ArgsParserTest, GetKeyValue) {
  int argc = 2;
  char* argv[] = {(char*)"program", (char*)"--key=value", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_TRUE(parser.Has("--key"));
  EXPECT_EQ(parser.Get("--key").value(), "value");
}

TEST(ArgsParserTest, GetPositional) {
  int argc = 3;
  char* argv[] = {(char*)"program", (char*)"pos1", (char*)"pos2", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_TRUE(parser.GetPositional(0).has_value());
  EXPECT_EQ(parser.GetPositional(0).value(), "pos1");
  EXPECT_TRUE(parser.GetPositional(1).has_value());
  EXPECT_EQ(parser.GetPositional(1).value(), "pos2");
  EXPECT_FALSE(parser.GetPositional(2).has_value());
}

TEST(ArgsParserTest, GetFlagWithValue) {
  int argc = 3;
  char* argv[] = {(char*)"program", (char*)"--flag", (char*)"value", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_TRUE(parser.Has("--flag"));
  EXPECT_EQ(parser.Get("--flag").value(), "value");
}

TEST(ArgsParserTest, GetInt) {
  int argc = 2;
  char* argv[] = {(char*)"program", (char*)"--port=8080", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_TRUE(parser.GetInt("--port").has_value());
  EXPECT_EQ(parser.GetInt("--port").value(), 8080);
}

TEST(ArgsParserTest, GetIntInvalid) {
  int argc = 2;
  char* argv[] = {(char*)"program", (char*)"--port=eighty", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_FALSE(parser.GetInt("--port").has_value());
}

TEST(ArgsParserTest, HasFlag) {
  int argc = 2;
  char* argv[] = {(char*)"program", (char*)"--verbose", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_TRUE(parser.Has("--verbose"));
  EXPECT_FALSE(parser.Has("--quiet"));
}

TEST(ArgsParserTest, MixedArguments) {
  int argc = 5;
  char* argv[] = {(char*)"program", (char*)"command", (char*)"--user=admin", (char*)"--port", (char*)"9000", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_EQ(parser.GetPositional(0).value(), "command");
  EXPECT_EQ(parser.Get("--user").value(), "admin");
  EXPECT_EQ(parser.Get("--port").value(), "9000");
  EXPECT_EQ(parser.GetInt("--port").value(), 9000);
}

TEST(ArgsParserTest, NonExistentOption) {
  int argc = 1;
  char* argv[] = {(char*)"program", nullptr};
  eillance::utils::ArgsParser parser(argc, argv);

  EXPECT_FALSE(parser.Get("--nonexistent").has_value());
  EXPECT_FALSE(parser.GetInt("--nonexistent").has_value());
  EXPECT_FALSE(parser.Has("--nonexistent"));
}
