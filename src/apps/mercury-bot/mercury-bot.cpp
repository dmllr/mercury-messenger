#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/args_parser.hpp"
#include "mercury/bot/bot.hpp"

namespace {

// Global flag for signal handling
std::sig_atomic_t volatile gSignalStatus = 0;  // NOLINT(*-avoid-non-const-global-variables)

void SignalHandler(int const signal) {
  gSignalStatus = signal;
}

auto FormatFilenameTimestamp(std::uint64_t const ts_microseconds) -> std::string {
  std::time_t const seconds_since_epoch = ts_microseconds / 1000000ULL;
  std::tm tm_struct{};
#ifdef _WIN32
  localtime_s(&tm_struct, &seconds_since_epoch);
#else
  localtime_r(&seconds_since_epoch, &tm_struct);
#endif
  std::stringstream ss;
  auto const micros = ts_microseconds % 1000000;
  ss << std::put_time(&tm_struct, "%Y%m%d,%H%M%S") << "." << std::setfill('0') << std::setw(6) << micros;
  return ss.str();
}

}  // namespace

auto main(int argc, char** argv) -> int {
  static plog::ColorConsoleAppender<plog::TxtFormatter> console_appender;
  Log::Initialize(&console_appender, plog::info);

  eillance::utils::ArgsParser const args(argc, argv);
  auto const identity_dir_str = args.Get("--identity-dir");
  auto const output_dir_str = args.Get("--output-dir");

  if (!identity_dir_str) {
    LGE << "Missing required parameter: --identity-dir <path>";
    return 1;
  }
  if (!output_dir_str) {
    LGE << "Missing required parameter: --output-dir <path>";
    return 1;
  }

  std::filesystem::path const identity_path(*identity_dir_str);
  std::filesystem::path const output_path(*output_dir_str);

  if (!std::filesystem::is_directory(identity_path)) {
    LGE << "Identity directory does not exist: " << identity_path;
    return 1;
  }

  if (!std::filesystem::is_directory(output_path)) {
    LGI << "Output directory does not exist, creating it: " << output_path;
    if (!std::filesystem::create_directories(output_path)) {
      LGE << "Failed to create output directory.";
      return 1;
    }
  }

  // --- BOT CONFIGURATION ---
  mercury::bot::Bot::Options options;
  options.identity_path = identity_path;
  options.auto_accept_contacts = true;
  options.auto_reject_calls = true;

  try {
    mercury::bot::Bot const bot(options);

    LGI << "Bot initialized with ID: " << bot.GetId();

    // --- MESSAGE HANDLER ---
    bot.OnMessage([&output_path, &bot](mercury::bot::Message const& message) -> void {
      std::string const sender_prefix = message.from.id.substr(0, 8);
      LGI << "Received message from " << sender_prefix << "...: \"" << message.text << "\"";

      std::string const timestamp_str = FormatFilenameTimestamp(message.date);
      std::string const filename = timestamp_str + "," + sender_prefix + ".txt";
      auto const file_path = output_path / filename;

      if (std::ofstream out_file(file_path); out_file) {
        out_file << message.text;
        LGI << "Saved to " << file_path;
        bot.MarkAsRead(message.chat_id, message.message_id);
      } else {
        LGE << "Failed to write to file: " << file_path;
      }
    });

    bot.Start();

    // --- MAIN LOOP ---
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    LGI << "Bot is running. Press Ctrl+C to exit.";

    while (gSignalStatus == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LGI << "Shutdown signal received. Stopping bot...";
    bot.Stop();
  } catch (std::exception const& e) {
    LGE << "Fatal error: " << e.what();
    return 1;
  }

  LGI << "Exiting.";
  return 0;
}
