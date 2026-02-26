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
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"
#include "eillance/rtc/media_transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/utils/args_parser.hpp"
#include "eillance/utils/event_queue.hpp"

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

  // Setup Event Subscriptions for functional test reporting
  eillance::EventQueue::Instance().Subscribe<eillance::EventStatusChanged>([](auto const& e) {
    LGI << "[mock] " << e.peer_id.ToString() << " Status change " << static_cast<int>(e.source) << ":" << e.state;
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventChatMessageReceived>([](auto const& e) {
    if (e.message.direction == eillance::messaging::MessageDirection::received) {
      if (std::holds_alternative<eillance::messaging::MessageEntry>(e.message.data)) {
        auto const& [content, attachments] = std::get<eillance::messaging::MessageEntry>(e.message.data);
        LGI << "[mock] Message from " << e.peer_id.ToString() << " received: " << content;
      }
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventMessageStatusChanged>([](auto const& e) {
    if (e.status == eillance::messaging::MessageStatus::delivered) {
      LGI << "[mock] Message to " << e.peer_id.ToString() << " delivered";
    } else if (e.status == eillance::messaging::MessageStatus::read) {
      LGI << "[mock] Message to " << e.peer_id.ToString() << " read";
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventAttachmentStatusChanged>([](auto const& e) {
    LGI << "[mock] File transfer status changed to " << static_cast<int>(e.status);
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventMyProfileUpdated>([](auto const&) {
    LGI << "[mock] My profile updated";
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventAttachmentDownloaded>([](auto const& e) {
    LGI << "[mock] Attachment " << e.attachment_id << " downloaded from " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventTransferProgress>([](auto const& e) {
    if (e.progress > 0.999999999F) {
      LGI << "[mock] Transfer " << e.attachment_id << " complete";
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactRequest>([](auto const& e) {
    LGI << "[mock] Contact request from " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactStatusChanged>([](auto const& e) {
    LGI << "[mock] Contact " << e.peer_id.ToString() << " status: " << static_cast<int>(e.status);
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactRemoved>([](auto const& e) {
    LGI << "[mock] Contact removed: " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactProfileUpdated>([](auto const& e) {
    LGI << "[mock] Profile updated for " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventIncomingCall>([](auto const& e) {
    LGI << "[mock] Incoming call from " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallRinging>([](auto const& e) {
    LGI << "[mock] Call ringing " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallAccepted>([](auto const& e) {
    LGI << "[mock] Call accepted " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallTerminated>([](auto const& e) {
    LGI << "[mock] Call terminated " << e.peer_id.ToString();
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

  // mkfile <path> <size>
  std::regex const re_mkfile(R"(mkfile\s+(\S+)\s+(\d+))");

  for (auto const& cmd : commands) {
    LGI << "Executing: " << cmd;

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
        LGI << "[mock] Generated file " << path << " (" << size << " bytes)";
      }
    } else if (cmd.starts_with("profile")) {
      if (std::smatch match; std::regex_match(cmd, match, re_profile)) {
        client->UpdateMyProfile(match[1].str(), match[2].str(), std::nullopt);
        LGI << "[mock] Executed profile update: " << match[1].str();
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
        LGI << "[mock] Message with " << atts.size() << " attachments sent to " << peer_hex;
      }
    } else if (cmd.starts_with("message")) {
      if (std::smatch match; std::regex_match(cmd, match, re_msg)) {
        std::string const peer_hex = match[1].str();
        std::string const content = match[2].str();
        eillance::ContactID const peer_id(peer_hex);

        std::ignore = client->AddContact(peer_id, std::nullopt);
        client->ConfirmContact(peer_id);

        std::ignore = client->SendChatMessage(peer_id, content);
        LGI << "[mock] Message to " << peer_hex << " sent";
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
