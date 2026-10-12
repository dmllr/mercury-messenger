#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

#include <openssl/rand.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include "eillance/logging/logging.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/transport/network_config.hpp"
#include "eillance/utils/args_parser.hpp"

namespace {

std::sig_atomic_t volatile signalReceived = 0;
void SignalHandler(int const signum) {
  signalReceived = signum;
}

constexpr auto ParseLogLevel(std::string_view const level) -> plog::Severity {
  if (level == "none") {
    return plog::none;
  }
  if (level == "fatal") {
    return plog::fatal;
  }
  if (level == "error") {
    return plog::error;
  }
  if (level == "warning") {
    return plog::warning;
  }
  if (level == "info") {
    return plog::info;
  }
  if (level == "debug") {
    return plog::debug;
  }
  if (level == "verbose") {
    return plog::verbose;
  }
  return plog::info;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
#if defined(__unix__) || defined(__APPLE__)
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rl);
  }
#endif

  eillance::utils::ArgsParser const args(argc, argv);

  static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
  plog::init(plog::warning, &appender);
  auto const log_level_str = args.Get("--log-level").value_or("info");
  auto const log_level = ParseLogLevel(log_level_str);
  plog::init<logInstanceId>(log_level, &appender);

  uint16_t const port = static_cast<uint16_t>(args.GetInt("--port").value_or(10807));

  LGD << "Starting bootstrap node on 0.0.0.0:" << port;

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

  eillance::transport::Libp2pNode const node(secret_key, eillance::transport::NodeRole::bootstrapNode);
  node.SetTurnEligibility(true);
  node.Start();

  // Listen on the specified port.
  // IMPORTANT: For DHT to work properly, we listen on 0.0.0.0
  std::string const listen_addr = "/ip4/0.0.0.0/tcp/" + std::to_string(port);
  node.Listen(listen_addr);

  auto const default_urls = eillance::transport::ParseUrls(eillance::transport::GetDefaultBootstrapNodeUrls());
  for (auto const& url : default_urls) {
    LGD << "Adding peer bootstrap node: " << url;
    node.AddBootstrapNode(url);
  }

  LGI << "Bootstrap Node Active. Multiaddr: " << listen_addr;
  std::signal(SIGINT, SignalHandler);
  while (signalReceived == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  node.Stop();
  return 0;
}
