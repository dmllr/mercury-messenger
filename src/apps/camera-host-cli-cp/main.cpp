#include <iostream>

#include "hela/eillance/capture/capture_manager.hpp"
#include "hela/eillance/rtc/producer.hpp"
#include "hela/eillance/rtc/transport.hpp"


auto main(int  /*argc*/, char**  /*argv*/) -> int {
  using namespace hela::eillance;
  auto cm = std::make_shared<CaptureManager>();
  auto rtc = RtcTransport<RtcProducer>(cm);
  rtc.Start("server");

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
