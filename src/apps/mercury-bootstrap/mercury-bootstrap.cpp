#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

#include <openssl/rand.h>

#include "eillance/logging/logging.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/utils/args_parser.hpp"

namespace {
std::sig_atomic_t volatile signal_received = 0;
void SignalHandler(int const signum) {
  signal_received = signum;
}
}  // namespace

auto main(int argc, char* argv[]) -> int {
  eillance::utils::ArgsParser const args(argc, argv);
  uint16_t const port = static_cast<uint16_t>(args.GetInt("--port").value_or(10807));

  // Generate a random unique identity for each bootstrap node run.
  // This is required if we run multiple instances to avoid DHT routing collisions.
  std::array<uint8_t, 32> rand_buf{};
  if (RAND_bytes(rand_buf.data(), static_cast<int>(rand_buf.size())) != 1) {
    LGE << "Failed to generate random seed for bootstrap node.";
    return 1;
  }

  std::vector<std::byte> secret_key;
  secret_key.reserve(rand_buf.size());
  for (auto b : rand_buf) {
    secret_key.push_back(static_cast<std::byte>(b));
  }

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
