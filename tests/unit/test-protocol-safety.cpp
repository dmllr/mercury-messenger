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

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

namespace {

// --- Schema Signature Generation ---

// Generates a deterministic signature of the JSON structure (keys and types only).
// e.g. { "type": "string", "id": "number" } -> "{id:number,type:string}"
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
      // Use .at() for const-correct access which throws instead of asserting
      ss << keys[i] << ":" << GenerateTypeSignature(j.at(keys[i]));
      if (i < keys.size() - 1) ss << ",";
    }
    ss << "}";
    return ss.str();
  } else if (j.is_array()) {
    std::stringstream ss;
    ss << "[";
    if (!j.empty()) {
      // Assume homogeneous array or check just the first one for schema definition
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

// Mock Capture to satisfy MediaCallService invariants
class MockCapture {
 public:
  void Configure(Config) {}
  void Engage() {}
  void Revoke() {}
  auto FetchBuffers() -> Packet { return Packet(); }
  auto GetSampleDuration() -> Time::Unit { return 10000; }
};

}  // namespace

class ProtocolSafetyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 1. Setup Temp Directory for File operations
    temp_dir_ = std::filesystem::temp_directory_path() / "protocol_safety_tests";
    std::filesystem::create_directories(temp_dir_);

    // 2. Setup Services
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
    account_service_ = std::make_unique<services::AccountService>(storage_, transport_);
    // Prevent invariant crash
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    (void)account_service_->GenerateNewIdentity();

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    messaging_service_ = std::make_unique<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        [](auto) {}
    );

    // Dummy callbacks
    messaging_service_->SetChatMessageReceivedCallback([](auto, auto) {});
    messaging_service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
    messaging_service_->SetAttachmentStatusChangedCallback([](auto, auto, auto) {});
    messaging_service_->SetTransferProgressCallback([](auto, auto, auto) {});
    messaging_service_->SetAttachmentDownloadedCallback([](auto, auto) {});

    profile_service_ = std::make_unique<services::ProfileService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_.get(),
        messaging_service_.get(),
        [](auto) {}
    );

    // 3. Establish a valid peer connection with crypto
    contact_manager_->AddContact(peer_id_);
    auto peer = std::make_shared<transport::Peer>(peer_id_);
    transport_->AddPeer(peer_id_, peer);

    // Perform handshake so we can decrypt what the service sends
    peer->data_channel_is_open = true;
    peer->session_cipher->ComputeSharedSecret(test_verifier_cipher_.GetPublicKey());
    test_verifier_cipher_.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  }

  void TearDown() override {
    if (messaging_service_) {
      messaging_service_->StopDeliveryService();
    }

    // Explicitly release services to ensure all file handles are closed
    // before attempting to remove the temporary directory.
    // FileTransferService keeps file streams open in 'active_uploads_'.
    profile_service_.reset();
    messaging_service_.reset();
    file_transfer_service_.reset();
    account_service_.reset();
    contact_manager_.reset();
    transport_.reset();
    storage_.reset();

    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    if (ec) {
      // Fallback: try to remove just the known file if directory removal fails (e.g. Windows latency)
      std::filesystem::remove(temp_dir_ / "image.jpg", ec);
      std::filesystem::remove(temp_dir_, ec);
    }
  }

  // Helper to extract the last message of a specific type sent to the peer.
  // This is necessary because some operations (like sending attachments) trigger
  // multiple messages (chat + file_resume_request), so .back() is not reliable.
  nlohmann::json GetLastMessageByType(std::string const& type) {
    if (mock_transport_->log.sent_peer_messages.empty()) {
      throw std::runtime_error("No messages sent");
    }
    // Iterate backwards to find the most recent message of the requested type
    auto const& messages = mock_transport_->log.sent_peer_messages;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
      if (it->second.value("type", "") == type) {
        return it->second;
      }
    }
    throw std::runtime_error("Message of type '" + type + "' not found in transport log");
  }

  // Decrypts payload from a JSON message using the test verifier cipher
  std::string DecryptPayload(nlohmann::json const& msg) {
    // Use .at() to throw if key is missing, preventing assertion failures
    if (!msg.contains("payload")) {
      throw std::runtime_error("Message structure invalid: 'payload' missing in " + msg.dump());
    }
    auto encrypted_bytes = msg.at("payload").get<std::vector<std::byte>>();
    return test_verifier_cipher_.Decrypt(encrypted_bytes);
  }

  std::filesystem::path temp_dir_;
  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::unique_ptr<services::MessagingService> messaging_service_;
  std::unique_ptr<services::ProfileService> profile_service_;

  eillance::crypto::SessionCipher test_verifier_cipher_;
  ContactID const peer_id_ = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

// =========================================================================================
// REAL PROTOCOL SIGNATURE VERIFICATION
// =========================================================================================

TEST_F(ProtocolSafetyTest, VerifyRealMessageSignatures) {
  std::stringstream combined_signatures;

  // ---------------------------------------------------------------------------------------
  // 1. Text Message (Encrypted)
  // ---------------------------------------------------------------------------------------
  messaging_service_->SendChatMessage(peer_id_, "Hello Protocol");
  auto msg_chat = GetLastMessageByType("chat");
  std::string sig_chat = GenerateTypeSignature(msg_chat);

  // Decrypt inner payload to verify E2EE structure
  auto plain_chat = DecryptPayload(msg_chat);
  auto json_chat = nlohmann::json::parse(plain_chat);
  std::string sig_chat_inner = GenerateTypeSignature(json_chat);

  combined_signatures << "CHAT_OUTER:" << sig_chat << "|CHAT_INNER:" << sig_chat_inner << "\n";

  // ---------------------------------------------------------------------------------------
  // 2. Attachment Message
  // ---------------------------------------------------------------------------------------
  // Create dummy file
  std::filesystem::path file_path = temp_dir_ / "image.jpg";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "dummy_content";
  }
  std::vector<services::AttachmentRequest> atts;
  atts.push_back({file_path.string(), messaging::AttachmentType::image, "thumb_base64", "image/jpeg"});

  messaging_service_->SendChatMessage(peer_id_, "File Attached", atts);
  // Get the CHAT message explicitly. The operation also sends a 'file_resume_request'
  // which we skip here by filtering for 'chat'.
  auto msg_att = GetLastMessageByType("chat");
  std::string sig_att = GenerateTypeSignature(msg_att);

  // Inner payload is just text content, attachments are outer.
  auto plain_att = DecryptPayload(msg_att);
  auto json_att = nlohmann::json::parse(plain_att);
  std::string sig_att_inner = GenerateTypeSignature(json_att);

  combined_signatures << "ATT_OUTER:" << sig_att << "|ATT_INNER:" << sig_att_inner << "\n";

  // ---------------------------------------------------------------------------------------
  // 3. Message Receipt (Delivery)
  // ---------------------------------------------------------------------------------------
  // Simulate receiving a message to trigger receipt
  // We manually construct the event to trigger the service logic
  // (OnChatMessageReceived calls SendPeerDataChannelMessage)
  nlohmann::json payload;
  payload[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload[services::MessagingService::keyContent] = "Incoming";
  auto encrypted = test_verifier_cipher_.Encrypt(payload.dump());

  messaging_service_->OnChatMessageReceived(peer_id_, encrypted, 999, false);
  auto msg_receipt = GetLastMessageByType("message_receipt");
  std::string sig_receipt = GenerateTypeSignature(msg_receipt);

  combined_signatures << "RECEIPT:" << sig_receipt << "\n";

  // ---------------------------------------------------------------------------------------
  // 4. Message Read Status
  // ---------------------------------------------------------------------------------------
  // Mock DB needs to know about the message to map IDs
  auto entry = mock_storage_->AddHistoryEntry(
      {.remote_id = 999,
       .kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .data = messaging::MessageEntry{"Incoming"},
       .peer_id = peer_id_}
  );

  messaging_service_->MarkMessagesAsRead(peer_id_, {entry.id});
  // MarkMessagesAsRead queues the action. We must flush the queue.
  messaging_service_->SendQueuedMessages(peer_id_);

  auto msg_read = GetLastMessageByType("message_read");
  std::string sig_read = GenerateTypeSignature(msg_read);

  combined_signatures << "READ:" << sig_read << "\n";

  // ---------------------------------------------------------------------------------------
  // 5. Profile Update
  // ---------------------------------------------------------------------------------------
  // Queue a profile send
  profile_service_->SendMyProfile(peer_id_);
  auto msg_profile = GetLastMessageByType("profile_info");
  std::string sig_profile = GenerateTypeSignature(msg_profile);

  combined_signatures << "PROFILE:" << sig_profile << "\n";

  // ---------------------------------------------------------------------------------------
  // Verification
  // ---------------------------------------------------------------------------------------
  std::string const final_signature_blob = combined_signatures.str();
  std::string const current_hash = crypto::Sha256Hex(final_signature_blob);

  // If you change the protocol format, update this hash and the version number.
  // Current Hash calculation based on implementation:
  // CHAT_OUTER: {is_system:boolean,message_id:number,payload:binary,type:string}
  // CHAT_INNER: {c:string,v:number}
  // ATT_OUTER:
  // {attachments:[{hash:string,id:string,mime:string,size:number,thumb:string,type:string}],is_system:boolean,message_id:number,payload:binary,type:string}
  // ATT_INNER:  {c:string,v:number}
  // RECEIPT:    {message_id:number,status:string,type:string}
  // READ:       {message_ids:[number],type:string}
  // PROFILE:    {bio:string,display_name:string,photo:null,type:string}  (photo is null in setup)

  std::string const expected_hash = "4388c0a0c4b32bcaa2ce309f75ba7e422541dcda8fad7111caf2a2f4595385d1";

  EXPECT_EQ(current_hash, expected_hash)
      << "\n[!] PROTOCOL SCHEMA CHANGED [!]\n"
      << "The wire format of one or more messages has changed.\n"
      << "Generated Signature Dump:\n"
      << final_signature_blob << "\n"
      << "Current Hash:  " << current_hash << "\n"
      << "Expected Hash: " << expected_hash << "\n"
      << "ACTION: Verify backward compatibility, increment protocol version if needed, and update this test.";
}
