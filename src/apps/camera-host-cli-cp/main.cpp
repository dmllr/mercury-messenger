#include <iostream>

#include "core/rtc/rtc_producer.hpp"


auto main(int  /*argc*/, char**  /*argv*/) -> int {
//  hela::eillance::VideoCapture cap;
//  cap.Engage();
//  cap.SetDataCallback([](auto) {});
//  cap.CaptureAndProcessNextFrame();

  using namespace hela::eillance;
  auto rtc = RtcTransport<RtcProducer>();
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
