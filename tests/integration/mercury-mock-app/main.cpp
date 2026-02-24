#include <chrono>
#include <filesystem>
#include <fstream>
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

struct Command {
  std::string name;
  std::vector<std::string> args;
};

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

void GenerateFile(std::string const& path, size_t size) {
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    return;
  }
  std::vector<char> const buffer(1024, 'A');
  size_t written = 0;
  while (written < size) {
    auto const to_write = std::min(size - written, buffer.size());
    ofs.write(buffer.data(), to_write);
    written += to_write;
  }
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
  client->SetStatusCallback(
      [](eillance::ContactID const& peer, eillance::EventSource const source, int const state) -> void {
        std::cout << "[mock] " << peer.ToString() << " Status change " << static_cast<int>(source) << ":" << state
                  << std::endl;
      }
  );

  client->GetMessagingService()->SetChatMessageReceivedCallback(
      [](eillance::ContactID const& peer, eillance::messaging::HistoryEntryRecord const& record) -> void {
        if (record.direction == eillance::messaging::MessageDirection::received) {
          if (std::holds_alternative<eillance::messaging::MessageEntry>(record.data)) {
            auto const& [content, attachments] = std::get<eillance::messaging::MessageEntry>(record.data);
            std::cout << "[mock] Message from " << peer.ToString() << " received: " << content << std::endl;
          }
        }
      }
  );
  client->GetMessagingService()->SetMessageStatusChangedCallback(
      [](eillance::ContactID const& peer, uint64_t, eillance::messaging::MessageStatus const status) -> void {
        if (status == eillance::messaging::MessageStatus::delivered) {
          std::cout << "[mock] Message to " << peer.ToString() << " delivered" << std::endl;
        } else if (status == eillance::messaging::MessageStatus::read) {
          std::cout << "[mock] Message to " << peer.ToString() << " read" << std::endl;
        }
      }
  );
  client->GetFileTransferService()->SetAttachmentStatusChangedCallback([](eillance::ContactID const&, std::string const&, eillance::messaging::AttachmentStatus status) -> void {
    std::cout << "[mock] File transfer status changed to " << static_cast<int>(status) << std::endl;
  });

  // Set dummy callbacks
  client->GetAccountService()->SetOnMyProfileUpdatedCallback([]() -> void {
    std::cout << "[mock] My profile updated" << std::endl;
  });

  client->GetMessagingService()->SetAttachmentDownloadedCallback(
      [](eillance::ContactID const& peer, std::string const& id) -> void {
        std::cout << "[mock] Attachment " << id << " downloaded from " << peer.ToString() << std::endl;
      }
  );

  client->GetMessagingService()->SetTransferProgressCallback(
      [](eillance::ContactID const&, std::string const& id, float const progress) -> void {
        if (progress > 0.999999999F) {
          std::cout << "[mock] Transfer " << id << " complete" << std::endl;
        }
      }
  );

  client->GetContactService()->SetContactRequestCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Contact request from " << peer.ToString() << std::endl;
  });

  client->GetContactService()->SetContactStatusChangedCallback(
      [](eillance::ContactID const& peer, eillance::identity::ContactStatus status) -> void {
        std::cout << "[mock] Contact " << peer.ToString() << " status: " << static_cast<int>(status) << std::endl;
      }
  );

  client->GetContactService()->SetContactRemovedCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Contact removed: " << peer.ToString() << std::endl;
  });

  client->GetProfileService()->SetContactProfileUpdatedCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Profile updated for " << peer.ToString() << std::endl;
  });

  client->GetMediaCallService()->SetIncomingCallCallback(
      [](eillance::ContactID const& peer, eillance::messaging::HistoryEntryKind kind) -> void {
        std::cout << "[mock] Incoming call from " << peer.ToString() << std::endl;
      }
  );

  client->GetMediaCallService()->SetCallRingingCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Call ringing " << peer.ToString() << std::endl;
  });

  client->GetMediaCallService()->SetCallAcceptedCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Call accepted " << peer.ToString() << std::endl;
  });

  client->GetMediaCallService()->SetCallTerminatedCallback([](eillance::ContactID const& peer) -> void {
    std::cout << "[mock] Call terminated " << peer.ToString() << std::endl;
  });

  if (!client->RestoreIdentity(*key)) {
    LGE << "Failed to restore identity from key: " << *key;
    return 1;
  }

  // --- Execute Script ---
  auto commands = ParseScript(*script_str);
  std::regex const re_wait(R"(wait\s+(\d+))");
  // message <id> "text"
  std::regex const re_msg(R"(message\s+([a-fA-F0-9]+)\s+\"([^\"]*)\")");
  // profile "Name" "Bio"
  std::regex const re_profile(R"(profile\s+\"([^\"]*)\"\s+\"([^\"]*)\")");
  // attach <id> "text" <path1> [<path2> ...]
  // This simple regex won't handle variable args well, so we'll do manual parsing for attachment commands

  // mkfile <path> <size>
  std::regex const re_mkfile(R"(mkfile\s+(\S+)\s+(\d+))");

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
    } else if (cmd.starts_with("mkfile")) {
      if (std::smatch match; std::regex_match(cmd, match, re_mkfile)) {
        std::string const path = match[1].str();
        size_t const size = std::stoul(match[2].str());
        GenerateFile(path, size);
        std::cout << "[mock] Generated file " << path << " (" << size << " bytes)" << std::endl;
      }
    } else if (cmd.starts_with("profile")) {
      if (std::smatch match; std::regex_match(cmd, match, re_profile)) {
        client->UpdateMyProfile(match[1].str(), match[2].str(), std::nullopt);
        std::cout << "[mock] Executed profile update: " << match[1].str() << std::endl;
      }
    } else if (cmd.starts_with("call ")) {
      // call <id>
      auto peer_hex = cmd.substr(5);
      // remove potential extra spaces
      peer_hex.erase(0, peer_hex.find_first_not_of(' '));
      peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);
      eillance::ContactID const peer_id(peer_hex);
      client->StartVoiceCall(peer_id);
    } else if (cmd.starts_with("accept ")) {
      auto peer_hex = cmd.substr(7);
      eillance::ContactID const peer_id(peer_hex);
      client->AcceptCall(peer_id);
    } else if (cmd.starts_with("reject ")) {
      auto peer_hex = cmd.substr(7);
      eillance::ContactID const peer_id(peer_hex);
      client->RejectCall(peer_id);
    } else if (cmd.starts_with("hangup ")) {
      auto peer_hex = cmd.substr(7);
      eillance::ContactID const peer_id(peer_hex);
      client->Hangup(peer_id, true);
    } else if (cmd.starts_with("attach")) {
      size_t const id_start = cmd.find(' ');
      size_t const quote_start = cmd.find('"', id_start + 1);
      size_t const quote_end = cmd.find('"', quote_start + 1);

      if (id_start != std::string::npos && quote_start != std::string::npos && quote_end != std::string::npos) {
        std::string peer_hex = cmd.substr(id_start + 1, quote_start - (id_start + 1));
        // trim
        peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);

        std::string const content = cmd.substr(quote_start + 1, quote_end - (quote_start + 1));
        std::string const paths_str = cmd.substr(quote_end + 1);

        eillance::ContactID const peer_id(peer_hex);
        std::ignore = client->AddContact(peer_id, std::nullopt);
        client->ConfirmContact(peer_id);

        std::vector<eillance::services::AttachmentRequest> atts;
        std::stringstream paths_ss(paths_str);
        std::string file_path;
        while (paths_ss >> file_path) {
          if (file_path.empty()) {
            continue;
          }
          atts.push_back(
              {.file_path = file_path,
               .type = eillance::messaging::AttachmentType::file,
               .mime_type = "application/octet-stream"}
          );
        }

        std::ignore = client->GetMessagingService()->SendChatMessage(peer_id, content, atts);
        std::cout << "[mock] Message with " << atts.size() << " attachments sent to " << peer_hex << std::endl;
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
