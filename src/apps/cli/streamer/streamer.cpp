#include <iostream>
#include <string>
#include <cstdlib>

#include "hela/eillance/rtc/streamer.hpp"
#include "hela/eillance/utils/string.hpp"

auto main(int argc, char** argv) -> int {
  using namespace hela::eillance;

  std::string streamer_id;
  if (argc > 1) {
    streamer_id = argv[1];
  } else {
    streamer_id = "Streamer-" + StringUtils::RandomString();
    std::cout << "Usage: " << argv[0] << " <your-unique-id>" << std::endl;
    std::cout << "No ID provided. Using random ID: " << streamer_id << std::endl;
  }

  auto rtc = RtcStreamer();
  rtc.SetId(streamer_id);
  rtc.SetName(streamer_id);
  rtc.Start();

  std::cout << "Streamer '" << streamer_id << "' is running. Press Enter to exit." << std::endl;

  std::string line;
  std::getline(std::cin, line);

  rtc.Terminate();
  return 0;
}
