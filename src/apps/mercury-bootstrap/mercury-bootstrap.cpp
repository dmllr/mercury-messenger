#include <chrono>
#include <csignal>
#include <thread>
#include <vector>
#include "eillance/logging/logging.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/utils/args_parser.hpp"

namespace {
std::sig_atomic_t volatile signal_received = 0;
void SignalHandler(int const signum) { signal_received = signum; }

// Hardcoded seed for the local bootstrap node identity.
// In production, this would be loaded from a protected file.
constexpr std::array<uint8_t, 32> kBootstrapSeed = {
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};
}

auto main(int argc, char* argv[]) -> int {
  eillance::utils::ArgsParser const args(argc, argv);
  uint16_t const port = static_cast<uint16_t>(args.GetInt("--port").value_or(807));

  std::vector<std::byte> secret_key;
  for(auto b : kBootstrapSeed) secret_key.push_back(static_cast<std::byte>(b));

  eillance::transport::Libp2pNode const node(secret_key);
  node.Start();

  // Listen on the specified port.
  // IMPORTANT: For DHT to work properly, we listen on 0.0.0.0
  std::string const listen_addr = "/ip4/0.0.0.0/tcp/" + std::to_string(port);
  node.Listen(listen_addr);

  LGI << "Bootstrap Node Active. Multiaddr: " << listen_addr;
  std::signal(SIGINT, SignalHandler);
  while (signal_received == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  node.Stop();
  return 0;
}
