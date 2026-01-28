#include <chrono>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/internal_messaging_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

namespace {

// Mock Capture to satisfy invariants in MediaCallService
class MockCapture {
 public:
  void Configure(Config) {}
  void Engage() {}
  void Revoke() {}
  auto FetchBuffers() -> Packet { return Packet(); }
  auto GetSampleDuration() -> Time::Unit { return 10000; }
};

}  // namespace

class ExtendedCoverageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 1. Storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // 2. Transport
    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    // 3. Dependencies
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);

    // Services
    account_service_ = std::make_shared<services::AccountService>(storage_, transport_);
    // Set callback to satisfy invariant during GenerateNewIdentity
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    (void)account_service_->GenerateNewIdentity();
    my_id_ = account_service_->GetId();

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    auto conn_handler = [](ContactID const&) {};

    messaging_service_ = std::make_shared<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        conn_handler
    );
    // Dummy callbacks to avoid bad_function_call
    messaging_service_->SetChatMessageReceivedCallback([](auto, auto) {});
    messaging_service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});

    media_call_service_ = std::make_shared<services::MediaCallService>(transport_);
    media_call_service_->SetVideoCapture(std::make_unique<MockCapture>());
    media_call_service_->SetAudioCapture(std::make_unique<MockCapture>());

    profile_service_ = std::make_shared<services::ProfileService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_.get(),
        messaging_service_.get(),
        conn_handler
    );

    contact_service_ = std::make_shared<services::ContactService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_.get(),
        profile_service_.get(),
        conn_handler
    );

    // 4. Target: Internal Messaging Service
    internal_service_ = std::make_unique<services::InternalMessagingService>(
        transport_,
        account_service_,
        contact_service_,
        messaging_service_,
        media_call_service_,
        file_transfer_service_,
        profile_service_
    );
  }

  void TearDown() override {
    messaging_service_->StopDeliveryService();
    file_transfer_service_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;
  std::shared_ptr<services::ProfileService> profile_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::unique_ptr<services::InternalMessagingService> internal_service_;

  ContactID my_id_;
};

TEST_F(ExtendedCoverageTest, MessagingService_ReceivesFutureVersion) {
  // 1. Setup Peer
  ContactID const peer_id("0000000000000000000000000000000000000000000000000000000000000002");
  auto peer = std::make_shared<transport::Peer>(peer_id);
  transport_->AddPeer(peer_id, peer);

  // Handshake crypto to enable decryption
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());

  // 2. Encrypt message with Version 255 (likely future)
  std::string payload;
  uint16_t future_version = 255;
  payload.push_back(static_cast<char>((future_version >> 8) & 0xFF));
  payload.push_back(static_cast<char>(future_version & 0xFF));
  payload.append("Future Message");
  auto encrypted = other.Encrypt(payload);

  bool received_system_msg = false;
  messaging_service_->SetChatMessageReceivedCallback([&](ContactID const& id,
                                                         messaging::HistoryEntryRecord const& rec) {
    if (id == peer_id && rec.kind == messaging::HistoryEntryKind::systemMessage) {
      auto const* msg = std::get_if<messaging::MessageEntry>(&rec.data);
      if (msg && msg->content.find("Unsupported message of version 255") != std::string::npos) {
        received_system_msg = true;
      }
    }
  });

  // 3. Inject message
  messaging_service_->OnChatMessageReceived(peer_id, encrypted, 101, false);

  EXPECT_TRUE(received_system_msg);
}

TEST_F(ExtendedCoverageTest, Glare_WhenMyIdIsLarger_YieldsToIncoming) {
  // We want MyID > PeerID.
  // We construct a PeerID that is all zeros, which is mathematically smaller than any random ID.
  ContactID const small_peer_id("0000000000000000000000000000000000000000000000000000000000000000");

  ASSERT_GT(my_id_, small_peer_id);

  // 1. Simulate we started a connection (Peer exists in transport)
  auto peer = std::make_shared<transport::Peer>(small_peer_id);
  transport_->AddPeer(small_peer_id, peer);

  // 2. Receive an OFFER from the smaller peer.
  // Rule: If MyID > PeerID, we yield our role (close/remove existing peer) and accept theirs.
  nlohmann::json const offer =
      {{"from", small_peer_id.ToString()}, {"type", "offer"}, {"context", "data"}, {"sdp", "dummy_sdp"}};

  internal_service_->ProcessSignalingMessage(small_peer_id, "offer", offer);

  // Verify we closed the old connection
  bool closed = false;
  for (auto const& [pid, ctx] : mock_transport_->log.closed_connections) {
    if (pid == small_peer_id && ctx == "data") {
      closed = true;
    }
  }
  EXPECT_TRUE(closed) << "Should close existing connection if MyID > PeerID";

  // Verify we created an ANSWER (meaning we accepted the new offer)
  bool answer_sent = false;
  for (auto const& [pid, ctx] : mock_transport_->log.answers) {
    if (pid == small_peer_id && ctx == "data") {
      answer_sent = true;
    }
  }
  EXPECT_TRUE(answer_sent) << "Should create answer for incoming offer";
}

TEST_F(ExtendedCoverageTest, Glare_WhenMyIdIsSmaller_IgnoresIncoming) {
  // We want MyID < PeerID.
  // We construct a PeerID that is max value.
  ContactID const large_peer_id("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");

  ASSERT_LT(my_id_, large_peer_id);

  // 1. Simulate we started a connection
  auto peer = std::make_shared<transport::Peer>(large_peer_id);
  transport_->AddPeer(large_peer_id, peer);

  // 2. Receive OFFER
  nlohmann::json const offer =
      {{"from", large_peer_id.ToString()}, {"type", "offer"}, {"context", "data"}, {"sdp", "dummy_sdp"}};

  internal_service_->ProcessSignalingMessage(large_peer_id, "offer", offer);

  // Verify NO answer sent (we ignored it)
  bool answer_sent = false;
  for (auto const& [pid, ctx] : mock_transport_->log.answers) {
    if (pid == large_peer_id) {
      answer_sent = true;
    }
  }
  EXPECT_FALSE(answer_sent) << "Should ignore incoming offer if MyID < PeerID (we persist as caller)";
}
