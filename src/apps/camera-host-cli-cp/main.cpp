#include <iostream>

#include "core/rtc/rtc_producer.hpp"
#include "core/stream/video_stream_source.hpp"

// tmp for OPUSFileParser
#include "fileparser.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif


auto main(int argc, char** argv) -> int {
  auto video = std::make_shared<VideoStreamSource>();
  auto audio = std::make_shared<OPUSFileParser>("/Users/denis.miller/dev/hela-surveillance/samples/opus", true);
  auto stream = std::make_shared<Stream>(video, audio);

  auto rtc = RtcProducer();
  rtc.setStream(stream);
  rtc.startServer();

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
