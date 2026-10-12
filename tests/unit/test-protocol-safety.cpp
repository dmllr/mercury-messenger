#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include "eillance/client/client.hpp"
#include "eillance/crypto/crypto.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

namespace {

// Generates a deterministic signature of the JSON structure (keys and types only).
std::string GenerateTypeSignature(nlohmann::json const& j) {
  if (j.is_object()) {
    std::vector<std::string> keys;
    for (auto& [key, val] : j.items()) {
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    std::stringstream ss;
    ss << "{";
    for (size_t i = 0; i < keys.size(); ++i) {
      ss << keys[i] << ":" << GenerateTypeSignature(j.at(keys[i]));
      if (i < keys.size() - 1) ss << ",";
    }
    ss << "}";
    return ss.str();
  } else if (j.is_array()) {
    std::stringstream ss;
    ss << "[";
    if (!j.empty()) {
      ss << GenerateTypeSignature(j[0]);
    }
    ss << "]";
    return ss.str();
  } else if (j.is_string()) {
    return "string";
  } else if (j.is_number()) {
    return "number";
  } else if (j.is_boolean()) {
    return "boolean";
  } else if (j.is_null()) {
    return "null";
  } else if (j.is_binary()) {
    return "binary";
  }
  return "unknown";
}

}  // namespace

class ProtocolSafetyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "protocol_safety_tests";
    std::filesystem::create_directories(temp_dir_);

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    auto transport = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, transport, media_transport);

    std::ignore = client_->GenerateNewIdentity();

    client_->Start();

    // Establish a valid peer connection with crypto
    mock_storage_->UpdateContact({peer_id_, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();
    auto peer = std::make_shared<transport::Peer>(peer_id_);
    transport->AddPeer(peer_id_, peer);

    peer->data_channel_is_open = true;
    peer->session_cipher->ComputeSharedSecret(test_verifier_cipher_.GetPublicKey());
    test_verifier_cipher_.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();

    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  nlohmann::json GetLastMessageByType(std::string const& type) {
    if (mock_transport_->CallLog()->sent_peer_messages.empty()) {
      throw std::runtime_error("No messages sent");
    }
    auto const& messages = mock_transport_->CallLog()->sent_peer_messages;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
      if (it->second.value("type", "") == type) {
        return it->second;
      }
    }
    throw std::runtime_error("Message of type '" + type + "' not found in transport log");
  }

  std::string DecryptPayload(nlohmann::json const& msg) const {
    if (!msg.contains("payload")) {
      throw std::runtime_error("Message structure invalid: 'payload' missing in " + msg.dump());
    }
    auto const encrypted_bytes = msg.at("payload").get<std::vector<std::byte>>();
    return test_verifier_cipher_.Decrypt(encrypted_bytes);
  }

  std::filesystem::path temp_dir_;
  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  eillance::crypto::SessionCipher test_verifier_cipher_;
  ContactID const peer_id_ = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

// =========================================================================================
// REAL PROTOCOL SIGNATURE VERIFICATION
// =========================================================================================

TEST_F(ProtocolSafetyTest, VerifyRealMessageSignatures) {
  std::stringstream combined_signatures;

  // Text Message (Encrypted)
  client_->SendChatMessage(peer_id_, "Hello Protocol");
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
          if (msg.value("type", "") == "chat") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );
  auto msg_chat = GetLastMessageByType("chat");
  std::string const sig_chat = GenerateTypeSignature(msg_chat);

  auto plain_chat = DecryptPayload(msg_chat);
  auto json_chat = nlohmann::json::parse(plain_chat);
  std::string const sig_chat_inner = GenerateTypeSignature(json_chat);

  combined_signatures << "CHAT_OUTER:" << sig_chat << "|CHAT_INNER:" << sig_chat_inner << "\n";

  // Attachment Message
  std::filesystem::path const file_path = temp_dir_ / "image.jpg";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "dummy_content";
  }
  std::vector<messaging::AttachmentRequest> atts;
  atts.push_back({file_path.string(), messaging::AttachmentType::image, "thumb_base64", "image/jpeg"});

  size_t current_msg_count = mock_transport_->CallLog()->sent_peer_messages.size();
  client_->SendChatMessage(peer_id_, "File Attached", atts);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        return mock_transport_->CallLog()->sent_peer_messages.size() > current_msg_count;
      },
      std::chrono::milliseconds(2000)
  );
  auto msg_att = GetLastMessageByType("chat");
  std::string const sig_att = GenerateTypeSignature(msg_att);

  auto plain_att = DecryptPayload(msg_att);
  auto json_att = nlohmann::json::parse(plain_att);
  std::string const sig_att_inner = GenerateTypeSignature(json_att);

  combined_signatures << "ATT_OUTER:" << sig_att << "|ATT_INNER:" << sig_att_inner << "\n";

  // Message Receipt (Delivery)
  nlohmann::json payload;
  payload[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload[services::MessagingService::keyContent] = "Incoming";
  auto encrypted = test_verifier_cipher_.Encrypt(payload.dump());

  client_->Messaging()->OnChatMessageReceived(peer_id_, encrypted, 999, false, {});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
          if (msg.value("type", "") == "message_receipt") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  auto msg_receipt = GetLastMessageByType("message_receipt");
  std::string const sig_receipt = GenerateTypeSignature(msg_receipt);

  combined_signatures << "RECEIPT:" << sig_receipt << "\n";

  // Message Read Status
  auto entry = mock_storage_->AddHistoryEntry(
      {.remote_id = 999,
       .kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .data = messaging::MessageEntry{"Incoming"},
       .peer_id = peer_id_}
  );

  client_->MarkMessagesAsRead(peer_id_, {entry.id});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
          if (msg.value("type", "") == "message_read") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  auto msg_read = GetLastMessageByType("message_read");
  std::string const sig_read = GenerateTypeSignature(msg_read);

  combined_signatures << "READ:" << sig_read << "\n";

  // Profile Update
  client_->Profile()->SendMyProfile(peer_id_);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
          if (msg.value("type", "") == "profile_info") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );
  auto msg_profile = GetLastMessageByType("profile_info");
  std::string const sig_profile = GenerateTypeSignature(msg_profile);

  combined_signatures << "PROFILE:" << sig_profile << "\n";

  // System Message
  current_msg_count = mock_transport_->CallLog()->sent_peer_messages.size();
  client_->Messaging()->SendSystemMessage(peer_id_, R"({"action": "call_request"})", true);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        return mock_transport_->CallLog()->sent_peer_messages.size() > current_msg_count;
      },
      std::chrono::milliseconds(2000)
  );

  auto msg_sys = GetLastMessageByType("chat");
  std::string const sig_sys = GenerateTypeSignature(msg_sys);

  auto plain_sys = DecryptPayload(msg_sys);
  auto json_sys = nlohmann::json::parse(plain_sys);
  std::string const sig_sys_inner = GenerateTypeSignature(json_sys);

  combined_signatures << "SYS_OUTER:" << sig_sys << "|SYS_INNER:" << sig_sys_inner << "\n";

  // Verification
  std::string const final_signature_blob = combined_signatures.str();
  std::string const current_hash = crypto::Sha256Hex(final_signature_blob);

  std::string const expected_hash = "f7dae8a6b699a76919a46bb7c4d298127c9697ac30e11a663a3dbb6715433c1b";

  EXPECT_EQ(current_hash, expected_hash)
      << "\n[!] PROTOCOL SCHEMA CHANGED [!]\n"
      << "The wire format of one or more messages has changed.\n"
      << "Generated Signature Dump:\n"
      << final_signature_blob << "\n"
      << "Current Hash:  " << current_hash << "\n"
      << "Expected Hash: " << expected_hash << "\n"
      << "ACTION: Verify backward compatibility, increment protocol version if needed, and update this test.";
}
