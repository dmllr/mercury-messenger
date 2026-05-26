#include <gtest/gtest.h>

#include "eillance/identity/topic_id.hpp"

using namespace eillance;

TEST(TopicIDTest, DefaultConstructor) {
  TopicID const t;
  EXPECT_TRUE(t.IsEmpty());
}

TEST(TopicIDTest, StringViewConstructor) {
  std::string const hex(64, 'a');
  TopicID const t(hex);
  EXPECT_FALSE(t.IsEmpty());
  EXPECT_EQ(t.ToString(), hex);
}

TEST(TopicIDTest, ContactIDConstructor) {
  std::string const hex(64, 'b');
  ContactID const c(hex);
  TopicID const t(c);
  EXPECT_FALSE(t.IsEmpty());
  EXPECT_EQ(t.ToString(), hex);
}

TEST(TopicIDTest, Equality) {
  TopicID const t1(std::string(64, 'c'));
  TopicID const t2(std::string(64, 'c'));
  TopicID const t3(std::string(64, 'd'));
  EXPECT_EQ(t1, t2);
  EXPECT_NE(t1, t3);
}

TEST(TopicIDTest, Comparison) {
  TopicID const t1(std::string(64, '1'));
  TopicID const t2(std::string(64, '2'));
  EXPECT_LT(t1, t2);
  EXPECT_GT(t2, t1);
}
