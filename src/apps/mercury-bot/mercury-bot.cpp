#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/logging/logging.h"
#include "eillance/rtc/transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/utils/args_parser.hpp"
#include "eillance/utils/time.hpp"

namespace {

// Global flag for signal handling
volatile std::sig_atomic_t gSignalStatus = 0;

void SignalHandler(int signal) {
  gSignalStatus = signal;
}

std::string FormatFilenameTimestamp(Time::Unit ts_microseconds) {
  const std::time_t seconds_since_epoch = ts_microseconds / 1000000U;
  std::tm tm_struct{};
#ifdef _WIN32
  localtime_s(&tm_struct, &seconds_since_epoch);
#else
  localtime_r(&seconds_since_epoch, &tm_struct);
#endif
  std::stringstream ss;
  const auto micros = ts_microseconds % 1000000;
  ss << std::put_time(&tm_struct, "%Y%m%d,%H%M%S") << "." << std::setfill('0') << std::setw(6) << micros;
  return ss.str();
}

constexpr auto GetConnectionStatusString(const eillance::ServiceConnectionState state) -> std::string_view {
  switch (state) {
    case eillance::ServiceConnectionState::establishing:
      return "CONNECTING";
    case eillance::ServiceConnectionState::online:
      return "ONLINE";
    case eillance::ServiceConnectionState::failure:
      return "CONNECTION FAILED";
    case eillance::ServiceConnectionState::disconnected:
      return "OFFLINE";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

int main(int argc, char** argv) {
  static plog::ColorConsoleAppender<plog::TxtFormatter> console_appender;
  Log::Initialize(&console_appender, plog::info);

  const eillance::utils::ArgsParser args(argc, argv);
  const auto identity_dir_str = args.Get("--identity-dir");
  const auto output_dir_str = args.Get("--output-dir");

  if (!identity_dir_str) {
    LOGE << "Missing required parameter: --identity-dir <path>";
    return 1;
  }
  if (!output_dir_str) {
    LOGE << "Missing required parameter: --output-dir <path>";
    return 1;
  }

  const std::filesystem::path identity_path(*identity_dir_str);
  const std::filesystem::path output_path(*output_dir_str);

  if (!std::filesystem::is_directory(identity_path)) {
    LOGE << "Identity directory does not exist or is not a directory: " << identity_path;
    return 1;
  }

  if (!std::filesystem::is_directory(output_path)) {
    LOGI << "Output directory does not exist, creating it: " << output_path;
    if (!std::filesystem::create_directories(output_path)) {
      LOGE << "Failed to create output directory.";
      return 1;
    }
  }

  auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(identity_path / "mercury.db");
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
  auto rtc_transport_impl = std::make_unique<eillance::rtc::RtcTransport>();
  auto transport = std::make_shared<eillance::transport::Transport>(std::move(rtc_transport_impl));
  auto client = std::make_shared<eillance::Client>(storage, transport);

  if (!client->IdentityExists()) {
    LOGE << "Identity not found in: " << identity_path;
    LOGE << "This bot requires a pre-existing identity. Please create one using mercury-cli.";
    return 1;
  }

  LOGI << "Bot starting with ID: " << client->GetId();

  // --- BOT LOGIC SETUP ---
  client->SetContactRequestCallback([&client](const std::string& peer_id) {
    LOGI << "Received contact request from " << peer_id << ". Accepting automatically.";
    client->ConfirmContact(peer_id);
  });

  client->SetIncomingCallCallback([&client](const std::string& peer_id, eillance::messaging::HistoryEntryKind /*call_type*/) {
    LOGI << "Received incoming call from " << peer_id << ". Rejecting automatically.";
    client->RejectCall(peer_id);
  });

  client->SetChatMessageReceivedCallback([&output_path](const std::string& peer_id, const eillance::messaging::HistoryEntryRecord& record) {
    if (record.kind != eillance::messaging::HistoryEntryKind::textMessage) {
      return;  // Ignore system messages and call logs
    }

    const auto& message_entry = std::get<eillance::messaging::MessageEntry>(record.data);
    LOGI << "Received message from " << peer_id.substr(0, 8) << "...: \"" << message_entry.content << "\"";

    const std::string timestamp_str = FormatFilenameTimestamp(record.timestamp);
    const std::string sender_id_prefix = peer_id.substr(0, 8);
    const std::string filename = timestamp_str + "," + sender_id_prefix + ".txt";
    const auto file_path = output_path / filename;

    std::ofstream out_file(file_path);
    if (out_file) {
      out_file << message_entry.content;
      LOGI << "Message saved to " << file_path;
    } else {
      LOGE << "Failed to open file for writing: " << file_path;
    }
  });

  client->SetStatusCallback([](const std::string& peer_id, eillance::EventSource source, int state_val) {
    if (source == eillance::EventSource::system) {
      const auto state = static_cast<eillance::ServiceConnectionState>(state_val);
      LOGI << "Signaling Connection Status: " << GetConnectionStatusString(state);
    } else if (source == eillance::EventSource::peerConnectionState && !peer_id.empty()) {
      const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      // This can get noisy, so we log at debug level.
      LOGD << "Peer " << peer_id.substr(0, 8) << "... Connection Status: " << static_cast<int>(state);
    }
  });

  if (auto cli_url = args.Get("--signaling-url")) {
    client->UpdateSignalingUrl(*cli_url);
  }

  std::thread client_thread([&] { client->Start(); });

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  LOGI << "Bot is running. Press Ctrl+C to exit.";

  while (gSignalStatus == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOGI << "Shutdown signal received. Terminating client...";
  client->Terminate();
  if (client_thread.joinable()) {
    client_thread.join();
  }
  LOGI << "Client terminated. Exiting.";

  return 0;
}
