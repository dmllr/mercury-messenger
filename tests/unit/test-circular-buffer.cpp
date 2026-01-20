#include "eillance/utils/circular_buffer.Hpp"

#include <gtest/gtest.h>

TEST(CircularBufferTest, InitialState) {
  eillance::CircularBuffer<int, 5> buffer;
  EXPECT_TRUE(buffer.IsEmpty());
  EXPECT_FALSE(buffer.IsFull());
  EXPECT_EQ(buffer.Size(), 0);
}

TEST(CircularBufferTest, PushAndPop) {
  eillance::CircularBuffer<int, 3> buffer;
  buffer.Push(1);
  buffer.Push(2);
  ASSERT_EQ(buffer.Size(), 2);
  ASSERT_FALSE(buffer.IsEmpty());
  ASSERT_FALSE(buffer.IsFull());

  EXPECT_EQ(buffer.Pop(), 1);
  ASSERT_EQ(buffer.Size(), 1);

  EXPECT_EQ(buffer.Pop(), 2);
  ASSERT_EQ(buffer.Size(), 0);
  EXPECT_TRUE(buffer.IsEmpty());
}

TEST(CircularBufferTest, OverwriteWhenFull) {
  eillance::CircularBuffer<int, 3> buffer;
  buffer.Push(1);
  buffer.Push(2);
  buffer.Push(3);
  ASSERT_TRUE(buffer.IsFull());
  ASSERT_EQ(buffer.Size(), 3);

  buffer.Push(4);  // This should overwrite 1
  ASSERT_TRUE(buffer.IsFull());
  ASSERT_EQ(buffer.Size(), 3);

  EXPECT_EQ(buffer.Pop(), 2);
  EXPECT_EQ(buffer.Pop(), 3);
  EXPECT_EQ(buffer.Pop(), 4);
  EXPECT_TRUE(buffer.IsEmpty());
}

TEST(CircularBufferTest, PopOrder) {
  eillance::CircularBuffer<std::string, 4> buffer;
  buffer.Push("A");
  buffer.Push("B");
  buffer.Push("C");

  EXPECT_EQ(buffer.Pop(), "A");

  buffer.Push("D");
  buffer.Push("E");  // Fills the buffer. `B` is now the oldest element.

  // The buffer now contains B, C, D, E in order.
  EXPECT_EQ(buffer.Pop(), "B");
  EXPECT_EQ(buffer.Pop(), "C");
  EXPECT_EQ(buffer.Pop(), "D");
  EXPECT_EQ(buffer.Pop(), "E");
}

TEST(CircularBufferTest, Clear) {
  eillance::CircularBuffer<int, 5> buffer;
  buffer.Push(1);
  buffer.Push(2);
  buffer.Push(3);
  ASSERT_EQ(buffer.Size(), 3);

  buffer.Clear();
  EXPECT_TRUE(buffer.IsEmpty());
  EXPECT_EQ(buffer.Size(), 0);

  // Can still push after clearing
  buffer.Push(10);
  EXPECT_EQ(buffer.Size(), 1);
  EXPECT_EQ(buffer.Pop(), 10);
}

TEST(CircularBufferTest, SizeManagement) {
  eillance::CircularBuffer<int, 2> buffer;
  ASSERT_EQ(buffer.Size(), 0);

  buffer.Push(1);
  ASSERT_EQ(buffer.Size(), 1);

  buffer.Push(2);
  ASSERT_EQ(buffer.Size(), 2);

  buffer.Push(3);  // Overwrites 1
  ASSERT_EQ(buffer.Size(), 2);

  EXPECT_EQ(buffer.Pop(), 2);
  ASSERT_EQ(buffer.Size(), 1);

  EXPECT_EQ(buffer.Pop(), 3);
  ASSERT_EQ(buffer.Size(), 0);
}
