#include <iostream>

#include "core/rtc/rtc_producer.hpp"


auto main(int  /*argc*/, char**  /*argv*/) -> int {
  auto rtc = RtcProducer();
  rtc.StartServer();

  while (true) {
    std::string id;
    std::cout << "Enter to exit" << std::endl;
    std::cin >> id;
    std::cin.ignore();
    std::cout << "exiting" << std::endl;
    break;
  }

  return 0;
}
