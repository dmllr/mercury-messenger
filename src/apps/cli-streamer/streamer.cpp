#include <iostream>

#include "hela/eillance/rtc/streamer.hpp"

auto main(int /*argc*/, char** /*argv*/) -> int {
  using namespace hela::eillance;
  auto rtc = RtcStreamer();
  rtc.SetId("CLI::Streamer");
  rtc.SetName("CLI::Streamer");
  rtc.Start();
  rtc.UpdateAuthToken("SSKJhkjhdiuadiuh");
//  rtc.SetId("CLI::Streamer " + StringUtils::RandomString());
//  rtc.SetName("CLI::Streamer" + StringUtils::RandomString());

  while (true) {
    char key;
    std::cout << "Enter to restart" << std::endl;
    std::cin >> key;
    std::cin.ignore();

    rtc.Terminate();
  }

  return 0;
}
