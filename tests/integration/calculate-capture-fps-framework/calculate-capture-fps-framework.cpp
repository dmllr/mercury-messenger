#include <atomic>
#include <chrono>
#include <iostream>

#include "eillance/capture/video_capture.hpp"

int main(int /*argc*/, char** /*argv*/) {
  eillance::VideoCaptureCli cap;
  cap.Engage();

  std::atomic_int frames_captured = 0;

  const auto start_time = std::chrono::high_resolution_clock::now();

  constexpr int num_frames = 100;  // Number of frames to capture
  while (frames_captured < num_frames) {
    cap.FetchBuffers();
    frames_captured++;
  }
  cap.Revoke();

  const auto end_time = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  const auto fps = static_cast<double>(num_frames) / (duration.count() / 1000.0);
  std::cout << "Average FPS: " << fps << std::endl;

  return 0;
}
