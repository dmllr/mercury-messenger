#include <iostream>

#include "hela/eillance/rtc/streamer.hpp"
#include "hela/eillance/utils/string.hpp"


auto main(int /*argc*/, char** /*argv*/) -> int {
  using namespace hela::eillance;
  auto rtc = RtcStreamer();
  rtc.SetName("CLI::" + StringUtils::RandomString());
  rtc.Start();

  while (true) {
    char key;
    std::cout << "Enter to restart" << std::endl;
    std::cin >> key;
    std::cin.ignore();

    rtc.Terminate();
  }

  return 0;
}
