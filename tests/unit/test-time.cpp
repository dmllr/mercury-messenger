#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

TEST(TimeTest, Constants) {
  EXPECT_EQ(Time::mS, 1000ULL);
  EXPECT_EQ(Time::uS, 1000000ULL);
}

TEST(TimeTest, Monotonicity) {
  auto const t1 = Time::Now();
  // Ensure some time passes.
  // Using a loop to guarantee different clock ticks if resolution is low.
  Time::Unit t2 = t1;
  auto const start = std::chrono::steady_clock::now();
  while (t2 <= t1) {
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    t2 = Time::Now();
    // Safety break after 1 second
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) {
      break;
    }
  }

  EXPECT_GT(t2, t1);
}

TEST(TimeTest, TypeWidth) {
  // Ensure Time::Unit is at least 64-bit to prevent overflow for microseconds
  EXPECT_GE(sizeof(Time::Unit), 8);
}
