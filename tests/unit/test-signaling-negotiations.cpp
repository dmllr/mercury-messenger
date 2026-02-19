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
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

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
    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    media_transport_ = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

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
        account_service_,
        file_transfer_service_,
        conn_handler
    );
    // Dummy callbacks to avoid bad_function_call
    messaging_service_->SetChatMessageReceivedCallback([](auto, auto) {});
    messaging_service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});

    media_call_service_ =
        std::make_shared<services::MediaCallService>(storage_, transport_, media_transport_, account_service_);
    media_call_service_->SetVideoCapture(std::make_unique<MockCapture>());
    media_call_service_->SetAudioCapture(std::make_unique<MockCapture>());

    profile_service_ = std::make_shared<services::ProfileService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_,
        messaging_service_,
        conn_handler
    );

    contact_service_ = std::make_shared<services::ContactService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_,
        profile_service_,
        conn_handler
    );

    // Target: Internal Messaging Service
    internal_service_ = std::make_unique<services::InternalMessagingService>(
        transport_,
        media_transport_,
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
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
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

TEST_F(SignalingNegotiationTest, OfferTriggersPeerCreationAndAnswer) {
  // Use a PeerID that is smaller than MyID to ensure we accept the offer
  ContactID const peer_id("0000000000000000000000000000000000000000000000000000000000000002");
  ASSERT_GT(my_id_, peer_id);

  // Simulate Offer Arriving with bundled candidates (implicit in SDP)
  nlohmann::json const offer_msg = {
      {"from", peer_id.ToString()},
      {"type", "offer"},
      {"context", "data"},
      {"sdp", "dummy_sdp_offer_with_candidates"}
  };

  internal_service_->ProcessSignalingMessage(peer_id, "offer", offer_msg);

  // Verify Peer Created and Answered
  ASSERT_TRUE(mock_transport_->GetPeer(peer_id));

  bool answer_created = false;
  for (auto const& [pid, ctx] : mock_transport_->CallLog()->answers) {
    if (pid == peer_id && ctx == "data") {
      answer_created = true;
    }
  }
  EXPECT_TRUE(answer_created);
}

TEST_F(SignalingNegotiationTest, AnswerDoesNotRecreatePeer) {
  ContactID const peer_id("0000000000000000000000000000000000000000000000000000000000000002");

  // Create peer initially (simulating we sent the offer)
  auto peer = std::make_shared<transport::Peer>(peer_id);
  mock_transport_->AddPeer(peer_id, peer);

  nlohmann::json const answer_msg =
      {{"from", peer_id.ToString()}, {"type", "answer"}, {"context", "data"}, {"sdp", "dummy_sdp_answer"}};

  internal_service_->ProcessSignalingMessage(peer_id, "answer", answer_msg);

  // Should NOT create new offer or answer
  EXPECT_TRUE(mock_transport_->CallLog()->answers.empty());
  EXPECT_TRUE(mock_transport_->CallLog()->offers.empty());

  // Should set remote description
  // Note: MockTransport::SetRemoteDescription log check would confirm this
}
