#include "eillance/utils/circular_buffer.hpp"

#include <gtest/gtest.h>

TEST(CircularBufferTest, InitialState) {
  eillance::CircularBuffer<int, 5> buffer;
  EXPECT_TRUE(buffer.isEmpty());
  EXPECT_FALSE(buffer.isFull());
  EXPECT_EQ(buffer.size(), 0);
}

TEST(CircularBufferTest, PushAndPop) {
  eillance::CircularBuffer<int, 3> buffer;
  buffer.push(1);
  buffer.push(2);
  ASSERT_EQ(buffer.size(), 2);
  ASSERT_FALSE(buffer.isEmpty());
  ASSERT_FALSE(buffer.isFull());

  EXPECT_EQ(buffer.pop(), 1);
  ASSERT_EQ(buffer.size(), 1);

  EXPECT_EQ(buffer.pop(), 2);
  ASSERT_EQ(buffer.size(), 0);
  EXPECT_TRUE(buffer.isEmpty());
}

TEST(CircularBufferTest, OverwriteWhenFull) {
  eillance::CircularBuffer<int, 3> buffer;
  buffer.push(1);
  buffer.push(2);
  buffer.push(3);
  ASSERT_TRUE(buffer.isFull());
  ASSERT_EQ(buffer.size(), 3);

  buffer.push(4);  // This should overwrite 1
  ASSERT_TRUE(buffer.isFull());
  ASSERT_EQ(buffer.size(), 3);

  EXPECT_EQ(buffer.pop(), 2);
  EXPECT_EQ(buffer.pop(), 3);
  EXPECT_EQ(buffer.pop(), 4);
  EXPECT_TRUE(buffer.isEmpty());
}

TEST(CircularBufferTest, PopOrder) {
  eillance::CircularBuffer<std::string, 4> buffer;
  buffer.push("A");
  buffer.push("B");
  buffer.push("C");

  EXPECT_EQ(buffer.pop(), "A");

  buffer.push("D");
  buffer.push("E");  // Fills the buffer. `B` is now the oldest element.

  // The buffer now contains B, C, D, E in order.
  EXPECT_EQ(buffer.pop(), "B");
  EXPECT_EQ(buffer.pop(), "C");
  EXPECT_EQ(buffer.pop(), "D");
  EXPECT_EQ(buffer.pop(), "E");
}

TEST(CircularBufferTest, Clear) {
  eillance::CircularBuffer<int, 5> buffer;
  buffer.push(1);
  buffer.push(2);
  buffer.push(3);
  ASSERT_EQ(buffer.size(), 3);

  buffer.clear();
  EXPECT_TRUE(buffer.isEmpty());
  EXPECT_EQ(buffer.size(), 0);

  // Can still push after clearing
  buffer.push(10);
  EXPECT_EQ(buffer.size(), 1);
  EXPECT_EQ(buffer.pop(), 10);
}

TEST(CircularBufferTest, SizeManagement) {
  eillance::CircularBuffer<int, 2> buffer;
  ASSERT_EQ(buffer.size(), 0);

  buffer.push(1);
  ASSERT_EQ(buffer.size(), 1);

  buffer.push(2);
  ASSERT_EQ(buffer.size(), 2);

  buffer.push(3);  // Overwrites 1
  ASSERT_EQ(buffer.size(), 2);

  EXPECT_EQ(buffer.pop(), 2);
  ASSERT_EQ(buffer.size(), 1);

  EXPECT_EQ(buffer.pop(), 3);
  ASSERT_EQ(buffer.size(), 0);
}
