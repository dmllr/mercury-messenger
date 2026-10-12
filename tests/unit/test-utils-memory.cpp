#include <memory>

#include <gtest/gtest.h>

#include "eillance/utils/memory.hpp"

TEST(MemoryUtilsTest, MakeWeakPtr) {
  auto shared = std::make_shared<int>(42);
  auto weak = MakeWeakPtr(shared);

  EXPECT_FALSE(weak.expired());

  auto shared2 = weak.lock();
  ASSERT_NE(shared2, nullptr);
  EXPECT_EQ(*shared2, 42);

  // Reset original shared_ptr
  shared.reset();
  // weak_ptr should still be valid because shared2 holds a reference
  EXPECT_FALSE(weak.expired());

  // Reset shared2, now the object should be destroyed
  shared2.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(weak.lock(), nullptr);
}
