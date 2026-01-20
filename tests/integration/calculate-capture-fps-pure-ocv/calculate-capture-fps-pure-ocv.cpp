#include <chrono>
#include <iostream>

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

struct CrossPlatformProperties {
#if defined __ANDROID__
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANDROID;
#elif defined _WIN32
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_DSHOW;
#else
  static constexpr auto OpenCVCameraCapabilities = cv::CAP_ANY;
#endif
};

int main(int /*argc*/, char** /*argv*/) {
  cv::VideoCapture cap;
  cv::Mat cv_frame;

  cap.open(0, cv::CAP_DSHOW);

  // attempt to set desired resolution
  cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
  cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

  if (!cap.isOpened()) {
    std::cerr << "Can't open the capture device. The video source has not been started." << std::endl;
    return -1;
  }
  cap >> cv_frame;
  if (cv_frame.empty()) {
    std::cerr << "Can't capture a test frame. The video source has not been started." << std::endl;
    return -1;
  }

  // check what resolution was actually set
  auto const actual_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
  auto const actual_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));

  std::cout << "Capture resolution set to: " << actual_width << "x" << actual_height << std::endl;

  constexpr int num_frames = 100;  // Number of frames to capture
  auto frames_captured = 0;
  auto start_time = std::chrono::high_resolution_clock::now();
  while (frames_captured < num_frames) {
    cap >> cv_frame;
    frames_captured++;
  }
  std::cout << "\n";
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  auto fps = static_cast<double>(num_frames) / (duration.count() / 1000.0);
  std::cout << "Average FPS: " << fps << std::endl;

  return 0;
}
