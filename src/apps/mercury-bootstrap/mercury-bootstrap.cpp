#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <openssl/rand.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/utils/args_parser.hpp"

namespace {
std::sig_atomic_t volatile signal_received = 0;

void SignalHandler(int const signum) {
  LGI << "\nInterrupt signal (" << signum << ") received.\n";
  signal_received = signum;
}

constexpr auto ParseLogLevel(std::string_view const level) -> plog::Severity {
  if (level == "none") return plog::none;
  if (level == "fatal") return plog::fatal;
  if (level == "error") return plog::error;
  if (level == "warning") return plog::warning;
  if (level == "info") return plog::info;
  if (level == "debug") return plog::debug;
  if (level == "verbose") return plog::verbose;
  return plog::info;
}
}  // namespace

auto main(int argc, char* argv[]) -> int {
  eillance::utils::ArgsParser const args(argc, argv);

  static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
  plog::init(plog::warning, &appender);
  auto log_level_str = args.Get("--log-level").value_or("info");
  plog::init<logInstanceId>(ParseLogLevel(log_level_str), &appender);

  LGI << "Starting Mercury Bootstrap Node";

  std::string key_file = args.Get("--key-file").value_or("bootstrap.key");
  std::vector<std::byte> secret_key(32);

  if (std::filesystem::exists(key_file)) {
    std::ifstream ifs(key_file, std::ios::binary);
    ifs.read(reinterpret_cast<char*>(secret_key.data()), 32);
    LGI << "Loaded secret key from " << key_file;
  } else {
    RAND_bytes(reinterpret_cast<unsigned char*>(secret_key.data()), 32);
    std::ofstream ofs(key_file, std::ios::binary);
    ofs.write(reinterpret_cast<char const*>(secret_key.data()), 32);
    LGI << "Generated new secret key and saved to " << key_file;
  }

  uint16_t const port = static_cast<uint16_t>(args.GetInt("--port").value_or(807));

  std::signal(SIGINT, SignalHandler);

  eillance::transport::Libp2pNode const node(secret_key);
  node.Start();

  std::string const listen_addr = "/ip4/0.0.0.0/tcp/" + std::to_string(port);
  node.Listen(listen_addr);

  LGI << "Listening on " << listen_addr << "\nPress Ctrl+C to exit";
  while (signal_received == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  LGI << "Shutting down...";
  node.Stop();
  return 0;
}
