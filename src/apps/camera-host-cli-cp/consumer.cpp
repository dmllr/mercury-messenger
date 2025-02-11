#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>

#include "hela/eillance/rtc/consumer.hpp"
#include "hela/eillance/utils/string.hpp"


void Terminate() {
  std::cout << "Terminating after 100 frames." << std::endl;
  exit(0);
}

auto main(int /*argc*/, char** /*argv*/) -> int {
  int frame_count = 0;
  std::ofstream all_frames_file("_all.h264", std::ios::binary | std::ios::app);

  using namespace hela::eillance;
  auto rtc = RtcConsumer();
  rtc.SetName("CLI::" + StringUtils::RandomString());
  rtc.Start();
  rtc.SetClientChangedCallback([&rtc](const std::vector<ClientInfo>& /*clients*/){
    rtc.SendStreamRequest();
  });
  rtc.SetFrameReceivedCallback([&rtc, &frame_count, &all_frames_file](const rtc::binary& frame) {
    if (frame_count >= 100) {
      rtc.Stop();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      Terminate();
      return;
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

    rtc.Stop();
  }

  return 0;
}
