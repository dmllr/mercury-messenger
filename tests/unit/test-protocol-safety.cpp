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
auto GenerateTypeSignature(nlohmann::json const& j) -> std::string {
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
  }
  if (j.is_array()) {
    std::stringstream ss;
    ss << "[";
    if (!j.empty()) {
      ss << GenerateTypeSignature(j[0]);
    }
    ss << "]";
    return ss.str();
  }
  if (j.is_string()) {
    return "string";
  }
  if (j.is_number()) {
    return "number";
  }
  if (j.is_boolean()) {
    return "boolean";
  }
  if (j.is_null()) {
    return "null";
  }
  if (j.is_binary()) {
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
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);

    std::ignore = client_->GenerateNewIdentity();

    client_->Start();

    // Establish a valid peer connection with crypto
    mock_storage_->UpdateContact({peer_id_, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();

    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  auto GetLatestLedgerJson(TopicID topic) -> nlohmann::json {
    auto entry = storage_->GetLatestLedgerEntry(topic);
    if (!entry) {
      throw std::runtime_error("No entry");
    }
    return nlohmann::json::parse(entry->payload);
  }

  std::filesystem::path temp_dir_;
  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  eillance::crypto::SessionCipher test_verifier_cipher_;
  ContactID const peer_id_ = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

// =========================================================================================
// REAL PROTOCOL SIGNATURE VERIFICATION
// =========================================================================================

TEST_F(ProtocolSafetyTest, VerifyRealMessageSignatures) {
  std::stringstream combined_signatures;

  // Text Message
  client_->SendChatMessage(peer_id_, "Hello Protocol");
  auto msg_chat = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_chat = GenerateTypeSignature(msg_chat);
  combined_signatures << "CHAT_OUTER:" << sig_chat << "\n";

  // Attachment Message
  std::filesystem::path const file_path = temp_dir_ / "image.jpg";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    ofs << "dummy_content";
  }
  std::vector<messaging::AttachmentRequest> atts;
  atts.push_back({file_path.string(), messaging::AttachmentType::image, "thumb_base64", "image/jpeg"});
  client_->SendChatMessage(peer_id_, "File Attached", atts);

  auto msg_att = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_att = GenerateTypeSignature(msg_att);
  combined_signatures << "ATT_OUTER:" << sig_att << "\n";

  // Message Receipt (Delivery)
  nlohmann::json const action_payload = {{"type", "message_receipt"}, {"message_id", 999}, {"status", "delivered"}};
  storage::LedgerEntry dummy_receipt;
  dummy_receipt.topic_id = TopicID(peer_id_);
  dummy_receipt.author_id = peer_id_;
  dummy_receipt.type = storage::LedgerActionType::Message;
  dummy_receipt.payload = action_payload.dump();
  storage_->AddLedgerEntry(dummy_receipt);

  auto msg_receipt = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_receipt = GenerateTypeSignature(msg_receipt);

  combined_signatures << "RECEIPT:" << sig_receipt << "\n";

  // Message Read Status
  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .data = messaging::MessageEntry{"Incoming"},
       .peer_id = peer_id_}
  );

  client_->MarkMessagesAsRead(peer_id_, {entry.id});

  auto msg_read = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_read = GenerateTypeSignature(msg_read);

  combined_signatures << "READ:" << sig_read << "\n";

  // Profile Update
  client_->Profile()->SendMyProfile(peer_id_);
  auto msg_profile = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_profile = GenerateTypeSignature(msg_profile);

  combined_signatures << "PROFILE:" << sig_profile << "\n";

  // System Message
  client_->Messaging()->SendSystemMessage(peer_id_, R"({"action": "call_request"})", true);

  auto msg_sys = GetLatestLedgerJson(TopicID(peer_id_));
  std::string const sig_sys = GenerateTypeSignature(msg_sys);
  combined_signatures << "SYS_OUTER:" << sig_sys << "\n";

  // Verification
  std::string const final_signature_blob = combined_signatures.str();
  std::string const expected =
      "CHAT_OUTER:{is_system:boolean,message_id:number,payload:{c:string,v:number},timestamp:number,type:string}\n"
      "ATT_OUTER:{attachments:[{hash:string,id:string,mime:string,name:string,note:string,size:number,thumb:string,"
      "type:string}],is_system:boolean,message_id:number,payload:{c:string,v:number},timestamp:number,type:string}\n"
      "RECEIPT:{message_id:number,status:string,type:string}\n"
      "READ:{message_ids:[number],type:string}\n"
      "PROFILE:{bio:string,display_name:string,photo:null,type:string}\n"
      "SYS_OUTER:{is_system:boolean,message_id:number,payload:{c:string,v:number},timestamp:number,type:string}\n";

  EXPECT_EQ(final_signature_blob, expected);
}
