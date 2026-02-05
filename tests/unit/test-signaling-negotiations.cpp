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
  void Configure(Config) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> Packet {
    return Packet();
  }
  auto GetSampleDuration() -> Time::Unit {
    return 10000;
  }
};

}  // namespace

class SignalingNegotiationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // Transport
    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    // Dependencies
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);

    // Services
    account_service_ = std::make_shared<services::AccountService>(storage_, transport_);
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    std::ignore = account_service_->GenerateNewIdentity();
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

    // Target: Internal Messaging Service
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

TEST_F(SignalingNegotiationTest, CandidatesArrivingBeforeOfferAreBuffered) {
  // Use a PeerID that is smaller than MyID to ensure we accept the offer (Smaller Wins policy)
  ContactID const peer_id("0000000000000000000000000000000000000000000000000000000000000002");
  ASSERT_GT(my_id_, peer_id);

  // 1. Simulate Candidate Arriving FIRST
  nlohmann::json const candidate_msg = {
      {"type", "candidate"},
      {"candidate", "candidate:1 1 UDP ..."},
      {"sdpMid", "0"}
  };

  internal_service_->ProcessSignalingMessage(peer_id, "candidate", candidate_msg);

  // At this point, peer does not exist, so candidate should be buffered.
  ASSERT_FALSE(mock_transport_->GetPeer(peer_id));
  ASSERT_TRUE(mock_transport_->log.added_candidates.empty());

  // 2. Simulate Offer Arriving SECOND
  nlohmann::json const offer_msg = {
      {"from", peer_id.ToString()},
      {"type", "offer"},
      {"context", "data"},
      {"sdp", "dummy_sdp_offer"}
  };

  internal_service_->ProcessSignalingMessage(peer_id, "offer", offer_msg);

  // 3. Verify Peer Created and Answered
  ASSERT_TRUE(mock_transport_->GetPeer(peer_id));

  bool answer_created = false;
  for (auto const& [pid, ctx] : mock_transport_->log.answers) {
    if (pid == peer_id && ctx == "data") {
      answer_created = true;
    }
  }
  EXPECT_TRUE(answer_created);

  // 4. IMPORTANT: Verify the buffered candidate was applied to the transport
  ASSERT_EQ(mock_transport_->log.added_candidates.size(), 1);
  auto const& [pid, ctx, cand, mid] = mock_transport_->log.added_candidates[0];
  EXPECT_EQ(pid, peer_id);
  EXPECT_EQ(ctx, "data");
  EXPECT_EQ(cand, "candidate:1 1 UDP ...");
  EXPECT_EQ(mid, "0");
}

TEST_F(SignalingNegotiationTest, CandidatesAppliedImmediatelyIfPeerExists) {
  ContactID const peer_id("0000000000000000000000000000000000000000000000000000000000000002");

  // 1. Create Peer (Simulate Offer processed)
  nlohmann::json const offer_msg = {
      {"from", peer_id.ToString()},
      {"type", "offer"},
      {"context", "data"},
      {"sdp", "dummy_sdp_offer"}
  };
  internal_service_->ProcessSignalingMessage(peer_id, "offer", offer_msg);
  ASSERT_TRUE(mock_transport_->GetPeer(peer_id));

  mock_transport_->log.added_candidates.clear();

  // 2. Send Candidate
  nlohmann::json const candidate_msg = {
      {"type", "candidate"},
      {"candidate", "candidate:2 1 UDP ..."},
      {"sdpMid", "1"}
  };
  internal_service_->ProcessSignalingMessage(peer_id, "candidate", candidate_msg);

  // Should be applied immediately
  ASSERT_EQ(mock_transport_->log.added_candidates.size(), 1);
  auto const& [pid, ctx, cand, mid] = mock_transport_->log.added_candidates[0];
  EXPECT_EQ(cand, "candidate:2 1 UDP ...");
}
