#include <iostream>

#include "hela/eillance/rtc/producer.hpp"
#include "hela/eillance/utils/string.hpp"


auto main(int /*argc*/, char** /*argv*/) -> int {
  using namespace hela::eillance;
  auto rtc = RtcTransport<RtcProducer>(StringUtils::RandomString());
  rtc.Start();

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
