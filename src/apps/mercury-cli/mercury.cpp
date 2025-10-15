#include <iostream>
#include <string>
#include <thread>
#include "hela/eillance/rtc/client.hpp"
#include "hela/eillance/utils/string.hpp"

auto main(int argc, char** argv) -> int {
  namespace he = hela::eillance;

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <peer-to-call> [your-id]" << std::endl;
    return 1;
  }

  std::string const peer_to_call = argv[1];
  std::string my_id;

  if (argc > 2) {
    my_id = argv[2];
  } else {
    my_id = "mercury-cli-" + StringUtils::RandomString();
  }

  he::RtcClient client;
  client.SetId(my_id);
  client.SetName(my_id);

  client.SetFrameReceivedCallback([](const rtc::binary& frame) {
      std::cout << "Received video frame of size " << frame.size() << std::endl;
  });

  client.Start();

  std::cout << "My ID is: " << my_id << std::endl;
  std::cout << "Calling " << peer_to_call << " in 3 seconds..." << std::endl;

  std::this_thread::sleep_for(std::chrono::seconds(3));
  client.Call(peer_to_call);

  std::cout << "Call initiated. Press Enter to exit." << std::endl;
  std::string line;
  std::getline(std::cin, line);

  client.Terminate();

  return 0;
}
