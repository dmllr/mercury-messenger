#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>

#include "hela/eillance/rtc/viewer.hpp"

auto main(int /*argc*/, char** /*argv*/) -> int {
  int frame_count = 0;
  std::ofstream all_frames_file("_all.h264", std::ios::binary | std::ios::app);

  using namespace hela::eillance;
  auto rtc = RtcViewer();
  rtc.SetId("CLI::Viewer");
  rtc.Start();
  rtc.SetClientChangedCallback([&rtc](const std::vector<Device>& clients){
    if (clients.empty()) {
      return;
    }
    auto& client = clients.front();
    rtc.SendStreamRequest(client.id, Config{});
  });
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

  while (true) {
    char key;
    std::cout << "Enter to restart" << std::endl;
    std::cin >> key;
    std::cin.ignore();

    rtc.Terminate();
  }

  return 0;
}
