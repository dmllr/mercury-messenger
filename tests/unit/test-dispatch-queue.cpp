#include "hela/eillance/common/dispatch_queue.hpp"

#include <gtest/gtest.h>

namespace hela::eillance {

TEST(DispatchQueueTest, DispatchAndExecute) {
  DispatchQueue queue("TestQueue", 1);

  int counter = 0;
  queue.Dispatch([&counter]() { counter++; });

  // Wait for the operation to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_EQ(counter, 1);
}

TEST(DispatchQueueTest, DispatchAndMove) {
  DispatchQueue queue("TestQueue", 1);

  int counter = 0;
  queue.Dispatch(std::move([&counter]() { counter++; }));

  // Wait for the operation to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_EQ(counter, 1);
}

TEST(DispatchQueueTest, RemovePending) {
  DispatchQueue queue("TestQueue", 1);

  int counter = 0;
  queue.Dispatch([&counter]() { counter++; });

  // Remove the pending operation
  queue.RemovePending();

  // Wait for the operation to execute (it shouldn't)
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_EQ(counter, 0);
}

TEST(DispatchQueueTest, MainThread) {
  int counter = 0;
  DispatchQueue::MainThread().Dispatch([&counter]() { counter++; });

  // Wait for the operation to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_EQ(counter, 1);
}

}  // namespace hela::eillance
