#include <atomic>
#include <chrono>
#include <iostream>

#include "eillance/capture/video_capture.hpp"

auto main(int /*argc*/, char** /*argv*/) -> int {
  eillance::VideoCaptureCli cap;
  cap.Engage();

  std::atomic_int frames_captured = 0;

  auto const start_time = std::chrono::high_resolution_clock::now();

  constexpr int num_frames = 100;  // Number of frames to capture
  while (frames_captured < num_frames) {
    cap.FetchBuffers();
    frames_captured++;
  }
  cap.Revoke();

  auto const end_time = std::chrono::high_resolution_clock::now();
  auto const duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  auto const fps = static_cast<double>(num_frames) / (duration.count() / 1000.0);
  std::cout << "Average FPS: " << fps << std::endl;

  return 0;
}
