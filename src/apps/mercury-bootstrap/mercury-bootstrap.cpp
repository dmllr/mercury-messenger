#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>
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

constexpr std::uint32_t minPort = 1;
constexpr std::uint32_t maxPort = 65535;

auto ParsePort(std::string_view const str, std::uint16_t& out_port) -> bool {
  if (str.empty()) {
    return false;
  }
  std::uint32_t val = 0;
  for (char const c : str) {
    if (c < '0' || c > '9') {
      return false;
    }
    val = val * 10 + static_cast<std::uint32_t>(c - '0');
    if (val > maxPort) {
      return false;
    }
  }
  if (val < minPort) {
    return false;
  }
  out_port = static_cast<std::uint16_t>(val);
  return true;
}

auto ParsePortRange(std::string_view const str, std::uint16_t& out_min, std::uint16_t& out_max) -> bool {
  auto const sep = str.find_first_of("-:");
  if (sep == std::string_view::npos || sep == 0 || sep == str.size() - 1) {
    return false;
  }
  auto const min_str = str.substr(0, sep);
  auto const max_str = str.substr(sep + 1);
  if (!ParsePort(min_str, out_min) || !ParsePort(max_str, out_max)) {
    return false;
  }
  return out_min <= out_max;
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

  std::uint16_t port = 10807;
  if (auto const port_str = args.Get("--port"); port_str.has_value()) {
    if (!ParsePort(*port_str, port)) {
      LGE << "Invalid port specified for --port: " << *port_str << " (expected 1-65535)";
      return 1;
    }
  }

  std::string const identity_file = args.Get("--identity-file").value_or("bootstrap_identity.key");
  auto const turn_port_arg = args.Get("--turn-port");
  auto const turn_port_range_arg = args.Get("--turn-port-range");

  if (turn_port_arg.has_value() && turn_port_range_arg.has_value()) {
    LGE << "Error: Cannot specify both --turn-port and --turn-port-range";
    return 1;
  }

  std::optional<std::uint16_t> turn_port;
  if (turn_port_arg.has_value()) {
    std::uint16_t p = 0;
    if (!ParsePort(*turn_port_arg, p)) {
      LGE << "Invalid TURN port specified for --turn-port: " << *turn_port_arg << " (expected 1-65535)";
      return 1;
    }
    turn_port = p;
  }

  std::optional<std::pair<std::uint16_t, std::uint16_t>> turn_port_range;
  if (turn_port_range_arg.has_value()) {
    std::uint16_t min_p = 0;
    std::uint16_t max_p = 0;
    if (!ParsePortRange(*turn_port_range_arg, min_p, max_p)) {
      LGE << "Invalid TURN port range specified for --turn-port-range: " << *turn_port_range_arg
          << " (expected min-max with 1 <= min <= max <= 65535)";
      return 1;
    }
    turn_port_range = std::make_pair(min_p, max_p);
  }

  LGD << "Starting bootstrap node on 0.0.0.0:" << port;

  std::vector<std::byte> secret_key(32);
  bool loaded = false;

  if (std::filesystem::exists(identity_file)) {
    if (std::ifstream file(identity_file, std::ios::binary); file) {
      file.read(reinterpret_cast<char*>(secret_key.data()), static_cast<std::streamsize>(secret_key.size()));
      if (file.gcount() == 32) {
        loaded = true;
        LGI << "Loaded persistent identity from " << identity_file;
      } else {
        LGW << "Identity file " << identity_file << " is invalid (expected 32 bytes). Generating a new identity.";
      }
    } else {
      LGW << "Failed to open identity file " << identity_file << " for reading. Generating a new identity.";
    }
  }

  if (!loaded) {
    std::array<uint8_t, 32> rand_buf{};
    if (RAND_bytes(rand_buf.data(), static_cast<int>(rand_buf.size())) != 1) {
      LGE << "Failed to generate random seed for bootstrap node.";
      return 1;
    }
    for (size_t i = 0; i < 32; ++i) {
      secret_key[i] = static_cast<std::byte>(rand_buf[i]);
    }

    if (std::ofstream file(identity_file, std::ios::binary | std::ios::trunc); file) {
      file.write(reinterpret_cast<char const*>(secret_key.data()), static_cast<std::streamsize>(secret_key.size()));
      LGI << "Generated new persistent identity and saved to " << identity_file;
    } else {
      LGW << "Failed to save new identity to " << identity_file << ". It will be ephemeral.";
    }
  }

  eillance::transport::Libp2pNode const node(secret_key, eillance::transport::NodeRole::bootstrapNode);
  if (turn_port.has_value()) {
    node.SetTurnPort(*turn_port);
  } else if (turn_port_range.has_value()) {
    node.SetTurnPortRange(turn_port_range->first, turn_port_range->second);
  }
  node.SetTurnEligibility(true);
  node.Start();

  // Listen on the specified port.
  // IMPORTANT: For DHT to work properly, we listen on 0.0.0.0
  std::string const listen_addr = "/ip4/0.0.0.0/tcp/" + std::to_string(port);
  node.Listen(listen_addr);

  for (auto const default_urls = eillance::transport::ParseUrls(eillance::transport::GetDefaultBootstrapNodeUrls());
       auto const& url : default_urls) {
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
