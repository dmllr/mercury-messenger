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

  Log::InitializeDefault(plog::info);

  eillance::utils::ArgsParser const args(argc, argv);

  // --- Utility Mode: Generate Identity ---
  if (args.Has("--gen-identity")) {
    auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>("/tmp/identity_gen.db");
    auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
    std::ignore = storage->Open();

    auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();

    auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

    eillance::Client client(storage, media_transport);

    auto mnemonic = client.GenerateNewIdentity();

    // Force restore from mnemonic to guarantee the outputted ID strictly matches
    // the PBKDF2 derivation used by nodes during the tests.
    client.RestoreIdentity(mnemonic);
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

  auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();

  auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

  auto client = std::make_shared<eillance::Client>(storage, media_transport);

  client->UpdateBootstrapNodeUrls(*signaling_url);

  bool const auto_confirm = !args.Has("--no-auto-confirm");

  eillance::EventQueue::Instance().Subscribe<eillance::EventChatMessageReceived>([](auto const& e) -> void {
    if (e.message.direction == eillance::messaging::MessageDirection::received) {
      if (std::holds_alternative<eillance::messaging::MessageEntry>(e.message.data)) {
        auto const& message = std::get<eillance::messaging::MessageEntry>(e.message.data);
        if (e.message.kind != eillance::messaging::HistoryEntryKind::systemMessage) {
          LGI << "[mock] Message from " << e.peer_id.ToString() << " received: " << message.content;
          if (message.reply_to_id > 0) {
            LGI << "[mock] Message is a reply to: " << message.reply_to_id;
          }
        }
      } else if (std::holds_alternative<eillance::messaging::CallEntry>(e.message.data)) {
        LGI << "[mock] Missing call message from " << e.peer_id.ToString();
      }
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventMessageStatusChanged>([](auto const& e) -> void {
    if (e.status == eillance::messaging::MessageStatus::delivered) {
      LGI << "[mock] Message to " << e.peer_id.ToString() << " delivered";
    } else if (e.status == eillance::messaging::MessageStatus::read) {
      LGI << "[mock] Message to " << e.peer_id.ToString() << " read";
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventAttachmentStatusChanged>([](auto const& e) -> void {
    LGI << "[mock] File transfer status changed to " << static_cast<int>(e.status);
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventMyProfileUpdated>([](auto const&) -> void {
    LGI << "[mock] My profile updated";
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventAttachmentDownloaded>([](auto const& e) -> void {
    LGI << "[mock] Attachment " << e.attachment_id << " downloaded from " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventTransferProgress>([](auto const& e) -> void {
    if (e.progress > 0.999999999F) {
      LGI << "[mock] Transfer " << e.attachment_id << " complete";
    }
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactRequest>(
      [client, auto_confirm](auto const& e) -> void {
        LGI << "[mock] Contact request from " << e.peer_id.ToString();
        if (auto_confirm) {
          client->ConfirmContact(e.peer_id);
        }
      }
  );

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactStatusChanged>([](auto const& e) -> void {
    LGI << "[mock] Contact " << e.peer_id.ToString() << " status: " << static_cast<int>(e.status);
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactRemoved>([](auto const& e) -> void {
    LGI << "[mock] Contact removed: " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventContactProfileUpdated>([](auto const& e) -> void {
    LGI << "[mock] Profile updated for " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventIncomingCall>([](auto const& e) -> void {
    LGI << "[mock] Incoming call from " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallRinging>([](auto const& e) -> void {
    LGI << "[mock] Call ringing " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallAccepted>([](auto const& e) -> void {
    LGI << "[mock] Call accepted " << e.peer_id.ToString();
  });

  eillance::EventQueue::Instance().Subscribe<eillance::EventCallTerminated>([](auto const& e) -> void {
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
  // reply <id> "text"
  std::regex const re_reply(R"(reply\s+([a-fA-F0-9]+)\s+\"([^\"]*)\")");
  // attach <id> "text" <path1> [<path2> ...]

  // mkfile <path> <size>
  std::regex const re_mkfile(R"(mkfile\s+(\S+)\s+(\d+))");

  for (auto const& cmd : commands) {
    LGI << "Executing: " << cmd;

    if (cmd == "connect") {
      client->Start();
    } else if (cmd == "disconnect") {
      client->Terminate();
    } else if (cmd == "stop") {
      client->Terminate();
    } else if (cmd == "start") {
      client->Start();
    } else if (cmd == "reconnect") {
      client->GetSettingsManager()->SetBootstrapNodeUrls(*signaling_url);
      client->UpdateBootstrapNodeUrls(*signaling_url);
      LGI << "[mock] Executed reconnect";
    } else if (cmd.starts_with("confirm ")) {
      auto peer_hex = cmd.substr(8);
      peer_hex.erase(0, peer_hex.find_first_not_of(' '));
      peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);
      eillance::ContactID const peer_id(peer_hex);
      client->ConfirmContact(peer_id);
      LGI << "[mock] Executed confirm: " << peer_hex;
    } else if (cmd.starts_with("block ")) {
      auto peer_hex = cmd.substr(6);
      peer_hex.erase(0, peer_hex.find_first_not_of(' '));
      peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);
      eillance::ContactID const peer_id(peer_hex);
      client->BlockContact(peer_id, true);
      LGI << "[mock] Executed block: " << peer_hex;
    } else if (cmd.starts_with("unblock ")) {
      auto peer_hex = cmd.substr(8);
      peer_hex.erase(0, peer_hex.find_first_not_of(' '));
      peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);
      eillance::ContactID const peer_id(peer_hex);
      client->UnblockContact(peer_id);
      LGI << "[mock] Executed unblock: " << peer_hex;
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
        client->UpdateMyProfile(
            eillance::identity::ProfileUpdate{
                .display_name = match[1].str(),
                .bio = match[2].str(),
                .photo_base64 = std::nullopt
            }
        );
        LGI << "[mock] Executed profile update: " << match[1].str();
      }
    } else if (cmd.starts_with("call ")) {
      // call <id>
      auto peer_hex = cmd.substr(5);
      // remove potential extra spaces
      peer_hex.erase(0, peer_hex.find_first_not_of(' '));
      peer_hex.erase(peer_hex.find_last_not_of(' ') + 1);
      eillance::ContactID const peer_id(peer_hex);
      std::ignore = client->AddContact(peer_id, std::nullopt);
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

        std::vector<eillance::messaging::AttachmentRequest> atts;
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

        std::ignore = client->Messaging()->SendChatMessage(peer_id, content, atts);
        LGI << "[mock] Message with " << atts.size() << " attachments sent to " << peer_hex;
      }
    } else if (cmd.starts_with("message")) {
      if (std::smatch match; std::regex_match(cmd, match, re_msg)) {
        std::string const peer_hex = match[1].str();
        std::string const content = match[2].str();
        eillance::ContactID const peer_id(peer_hex);

        std::ignore = client->AddContact(peer_id, std::nullopt);

        std::ignore = client->SendChatMessage(peer_id, content);
        LGI << "[mock] Message to " << peer_hex << " sent";
      } else {
        LGE << "Invalid message command format: " << cmd;
      }
    } else if (cmd.starts_with("reply")) {
      if (std::smatch match; std::regex_match(cmd, match, re_reply)) {
        std::string const peer_hex = match[1].str();
        std::string const content = match[2].str();
        eillance::ContactID const peer_id(peer_hex);

        std::ignore = client->AddContact(peer_id, std::nullopt);

        auto page = client->GetMessageHistoryPage(peer_id, 0, 10);
        uint64_t reply_id = 0;
        for (auto& m : page.messages) {
          if (m.direction == eillance::messaging::MessageDirection::received) {
            reply_id = m.id;
            break;
          }
        }
        std::ignore = client->Messaging()->SendChatMessage(peer_id, content, {}, reply_id);
        LGI << "[mock] Reply to " << peer_hex << " sent";
      } else {
        LGE << "Invalid reply command format: " << cmd;
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
