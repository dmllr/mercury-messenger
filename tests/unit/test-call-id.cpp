#include <string>

#include <gtest/gtest.h>

#include "eillance/services/call_id.hpp"

using eillance::services::CallID;

TEST(CallIDTest, EmptyInitialization) {
  CallID const id;
  EXPECT_TRUE(id.IsEmpty());
  EXPECT_EQ(id.ToString(), "");
}

TEST(CallIDTest, StringInitialization) {
  CallID const id("test-call-id-123");
  EXPECT_FALSE(id.IsEmpty());
  EXPECT_EQ(id.ToString(), "test-call-id-123");
}

TEST(CallIDTest, Truncation) {
  std::string const long_str(100, 'X');
  CallID const id(long_str);
  // callIdSize is 40, so max chars = 39 (null-terminated inside CallID logic)
  EXPECT_EQ(id.ToString().length(), 39);
}

TEST(CallIDTest, Equality) {
  CallID const id1("same_id");
  CallID const id2("same_id");
  CallID const id3("different_id");

  EXPECT_TRUE(id1 == id2);
  EXPECT_FALSE(id1 == id3);
}