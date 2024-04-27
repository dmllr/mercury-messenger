#include <iostream>
#include <chrono>

#include "hela/eillance/rtc/producer.hpp"

int main(int /*argc*/, char** /*argv*/) {
  hela::eillance::VideoCapture cap;
  cap.Engage();
  cap.SetDataCallback([](auto) {});

  constexpr int num_frames = 100; // Number of frames to capture
  auto frames_captured = 0;
  auto start_time = std::chrono::high_resolution_clock::now();
  while (frames_captured < num_frames) {
    cap.CaptureAndProcessNextFrame();
    frames_captured++;
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  auto fps = static_cast<double>(num_frames) / (duration.count() / 1000.0);
  std::cout << "Average FPS: " << fps << std::endl;

  return 0;
}
