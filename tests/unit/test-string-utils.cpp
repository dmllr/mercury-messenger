#include "hela/eillance/utils/string.hpp"

#include <gtest/gtest.h>

TEST(StringUtils, randomStringTest) {
  const auto str1 = StringUtils::RandomString();
  ASSERT_EQ(str1.length(), 10);
  ASSERT_NE(str1[0], '\0');
  ASSERT_NE(str1[1], '\0');
  ASSERT_NE(str1[2], '\0');
  ASSERT_NE(str1[3], '\0');
  ASSERT_NE(str1[4], '\0');
  ASSERT_NE(str1[5], '\0');
  ASSERT_NE(str1[6], '\0');
  ASSERT_NE(str1[7], '\0');
  ASSERT_NE(str1[8], '\0');
  ASSERT_NE(str1[9], '\0');

  const auto str2 = StringUtils::RandomString();
  ASSERT_NE(str1, str2);
}
