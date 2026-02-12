#include <chrono>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"
#include "eillance/rtc/media_transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/utils/args_parser.hpp"

auto ParseScript(std::string const& script) -> std::vector<std::string> {
  std::vector<std::string> commands;
  std::stringstream script_stream(script);
  std::string segment;
  while (std::getline(script_stream, segment, ',')) {
    // Trim whitespace
    auto const start = segment.find_first_not_of(" \t\r\n");
    auto const end = segment.find_last_not_of(" \t\r\n");
    if (start != std::string::npos) {
      commands.push_back(segment.substr(start, end - start + 1));
    }
  }
  return commands;
}

auto main(int argc, char** argv) -> int {
  // Disable stdout buffering for real-time docker logs
  std::setbuf(stdout, nullptr);

  Log::InitializeDefault(plog::debug);

  eillance::utils::ArgsParser const args(argc, argv);

  // --- Utility Mode: Generate Identity ---
  if (args.Has("--gen-identity")) {
    auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>("/tmp/identity_gen.db");
    auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
    std::ignore = storage->Open();

    auto data_transport_impl = std::make_unique<eillance::rtc::RtcDataTransport>();
    auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();

    auto data_transport = std::make_shared<eillance::transport::DataTransport>(std::move(data_transport_impl));
    auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

    eillance::Client client(storage, data_transport, media_transport);

    // Set dummy callbacks to prevent crashes if any internals trigger them
    client.SetStatusCallback([](auto&&, auto, auto) {});
    client.SetDataReloadedCallback([]() {});
    client.GetAccountService()->SetOnMyProfileUpdatedCallback([]() {});

    auto mnemonic = client.GenerateNewIdentity();
    auto id = client.GetId().ToString();

    std::cout << "MNEMONIC: " << mnemonic << std::endl;
    std::cout << "ID: " << id << std::endl;
    return 0;
  }

  // --- Normal Mode ---
  auto key = args.Get("--key");
  auto script_str = args.Get("--script");
  auto signaling_url = args.Get("--signaling-url");
  auto db_path = args.Get("--db-path").value_or(":memory:");

  if (!key || !script_str || !signaling_url) {
    LGE << "Usage: mercury-mock-app --key <mnemonic> --script <commands> --signaling-url <url> [--db-path <path>]";
    return 1;
  }

  // --- Setup Client ---
  auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(db_path);
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));

  if (!storage->Open()) {
    LGE << "Failed to open storage at " << db_path;
    return 1;
  }

  auto data_transport_impl = std::make_unique<eillance::rtc::RtcDataTransport>();
  auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();

  auto data_transport = std::make_shared<eillance::transport::DataTransport>(std::move(data_transport_impl));
  auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

  auto client = std::make_shared<eillance::Client>(storage, data_transport, media_transport);

  client->UpdateSignalingUrl(*signaling_url);

  // Set minimal callbacks for functional test reporting
  data_transport->SetStatusCallback(
      [](eillance::ContactID const& peer, eillance::EventSource const source, int const state) -> void {
        std::cout << "[mock] " << peer.ToString() << " Status change " << static_cast<int>(source) << ":" << state
                  << std::endl;
      }
  );

  client->GetMessagingService()->SetChatMessageReceivedCallback(
      [](eillance::ContactID const& peer, eillance::messaging::HistoryEntryRecord const& record) -> void {
        if (std::holds_alternative<eillance::messaging::MessageEntry>(record.data)) {
          auto const& [content, attachments] = std::get<eillance::messaging::MessageEntry>(record.data);
          std::cout << "[mock] Message from " << peer.ToString() << " received: " << content << std::endl;
        }
      }
  );
  client->GetMessagingService()->SetMessageStatusChangedCallback(
      [](eillance::ContactID const& peer, uint64_t, eillance::messaging::MessageStatus const status) {
        if (status == eillance::messaging::MessageStatus::delivered) {
          std::cout << "[mock] Message to " << peer.ToString() << " delivered" << std::endl;
        }
      }
  );

  // Set dummy callbacks
  client->GetAccountService()->SetOnMyProfileUpdatedCallback([]() {});
  client->GetMessagingService()->SetAttachmentDownloadedCallback([](auto&&, auto&&) {});
  client->GetMessagingService()->SetTransferProgressCallback([](auto&&, auto&&, auto) {});
  client->GetMessagingService()->SetAttachmentStatusChangedCallback([](auto&&, auto&&, auto) {});
  client->GetContactService()->SetContactRequestCallback([](auto&&) {});
  client->GetContactService()->SetContactStatusChangedCallback([](auto&&, auto) {});
  client->GetContactService()->SetContactRemovedCallback([](auto&&) {});
  client->GetProfileService()->SetContactProfileUpdatedCallback([](auto&&) {});
  client->GetMediaCallService()->SetFrameReceivedCallback([](auto&&) {});
  client->GetMediaCallService()->SetIncomingCallCallback([](auto&&, auto) {});
  client->GetMediaCallService()->SetCallRingingCallback([](auto&&) {});
  client->GetMediaCallService()->SetCallAcceptedCallback([](auto&&) {});
  client->GetMediaCallService()->SetCallTerminatedCallback([](auto&&) {});

  if (!client->RestoreIdentity(*key)) {
    LGE << "Failed to restore identity from key: " << *key;
    return 1;
  }

  // --- Execute Script ---
  auto commands = ParseScript(*script_str);
  std::regex const re_wait(R"(wait\s+(\d+))");
  std::regex const re_msg(R"(message\s+([a-fA-F0-9]+)\s+\"([^\"]+)\")");

  for (auto const& cmd : commands) {
    std::cout << "Executing: " << cmd << std::endl;
    if (cmd == "connect") {
      client->Start();
    } else if (cmd == "disconnect") {
      client->Terminate();
    } else if (cmd.starts_with("wait")) {
      if (std::smatch match; std::regex_match(cmd, match, re_wait)) {
        int const seconds = std::stoi(match[1].str());
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
      }
    } else if (cmd.starts_with("message")) {
      if (std::smatch match; std::regex_match(cmd, match, re_msg)) {
        std::string const peer_hex = match[1].str();
        std::string const content = match[2].str();
        eillance::ContactID const peer_id(peer_hex);

        std::ignore = client->AddContact(peer_id, std::nullopt);
        client->ConfirmContact(peer_id);

        std::ignore = client->SendChatMessage(peer_id, content);
        std::cout << "[mock] Message to " << peer_hex << " sent" << std::endl;
      } else {
        LGE << "Invalid message command format: " << cmd;
      }
    } else {
      LGE << "Unknown command: " << cmd;
    }
  }

  constexpr std::uint32_t test_threshold = 500U;
  std::this_thread::sleep_for(std::chrono::milliseconds(test_threshold));
  client->Terminate();

  return 0;
}
