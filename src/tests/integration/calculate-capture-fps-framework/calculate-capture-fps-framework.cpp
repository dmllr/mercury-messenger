#include <atomic>
#include <iostream>
#include <chrono>

#include "hela/eillance/rtc/producer.hpp"

int main(int /*argc*/, char** /*argv*/) {
  hela::eillance::VideoCapture cap;
  cap.Engage();

  std::atomic_int frames_captured = 0;
  cap.SetDataCallback([&frames_captured](auto) {
    frames_captured++;
  });

  constexpr int num_frames = 100; // Number of frames to capture
  auto start_time = std::chrono::high_resolution_clock::now();
  while (frames_captured < num_frames) {
    cap.ProcessLastCapturedFrame();
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  auto fps = static_cast<double>(num_frames) / (duration.count() / 1000.0);
  std::cout << "Average FPS: " << fps << std::endl;

  return 0;
}
