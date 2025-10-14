#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <string>
#include <cstdlib>

#include "hela/eillance/rtc/viewer.hpp"
#include "hela/eillance/utils/string.hpp"

auto main(int argc, char** argv) -> int {
  using namespace hela::eillance;

  if (argc < 2) {
      std::cerr << "Usage: " << argv[0] << " <streamer-id> [your-id]" << std::endl;
      return 1;
  }

  std::string streamer_id = argv[1];
  std::string viewer_id;

  if (argc > 2) {
      viewer_id = argv[2];
  } else {
      viewer_id = "Viewer-" + StringUtils::RandomString();
      std::cout << "No viewer ID provided. Using random ID: " << viewer_id << std::endl;
  }

  int frame_count = 0;
  std::ofstream all_frames_file("_all.h264", std::ios::binary | std::ios::app);

  auto rtc = RtcViewer();
  rtc.SetId(viewer_id);
  rtc.SetName(viewer_id);

  rtc.SetFrameReceivedCallback([&rtc, &frame_count, &all_frames_file](const rtc::binary& frame) {
    if (frame_count >= 100) {
      rtc.Terminate();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "Terminating after 100 frames." << std::endl;
      exit(0);
    }

    // Create numbered filename
    std::ostringstream filename;
    filename << std::setw(3) << std::setfill('0') << frame_count + 1 << ".h264";

    // Save individual frame
    std::ofstream file(filename.str(), std::ios::binary);
    if (file) {
      file.write(reinterpret_cast<const char*>(frame.data()), frame.size());
    }

    // Append to _all.h264
    if (all_frames_file) {
      all_frames_file.write(reinterpret_cast<const char*>(frame.data()), frame.size());
    }

    frame_count++;
  });

  rtc.Start();

  std::cout << "Attempting to connect to streamer: " << streamer_id << std::endl;
  rtc.SendStreamRequest(streamer_id, Config{});


  std::cout << "Viewer '" << viewer_id << "' is running. Press Enter to exit." << std::endl;
  std::string line;
  std::getline(std::cin, line);

  rtc.Terminate();
  return 0;
}
