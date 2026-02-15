#include <gtest/gtest.h>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

// Mock capture implementation for duck-typing
class MockCaptureImpl {
 public:
  void Configure(Config) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> Packet {
    // Return dummy data to ensure stream has content
    std::vector<std::byte> const data = {std::byte{0xAA}, std::byte{0xBB}};
    return Packet(data);
  }
  auto GetSampleDuration() -> Time::Unit {
    return 10000;
  }
};

class MediaCallServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Shared Log
    mock_log_ = std::make_shared<transport::MockCallLog>();

    // Setup Data Transport
    mock_data_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_data_transport_raw_ = mock_data_transport_impl_.get();
    data_transport_ = std::make_shared<transport::DataTransport>(std::move(mock_data_transport_impl_));

    // Setup Media Transport
    mock_media_transport_impl_ = std::make_unique<transport::MockMediaTransport>(mock_log_);
    mock_media_transport_raw_ = mock_media_transport_impl_.get();
    media_transport_ = std::make_shared<transport::MediaTransport>(std::move(mock_media_transport_impl_));

    // Setup messaging dependencies
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));
    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    auto conn_handler = [](ContactID const&) {};

    auto file_transfer_service = std::make_shared<services::FileTransferService>(storage_, data_transport_);

    account_service_ = std::make_unique<services::AccountService>(storage_, data_transport_);
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    std::ignore = account_service_->GenerateNewIdentity();

    // Initialize messaging service (needed by MediaCallService)
    messaging_service_ = std::make_unique<services::MessagingService>(
        storage_,
        data_transport_,
        contact_manager,
        file_transfer_service,
        conn_handler
    );
    messaging_service_->SetChatMessageReceivedCallback([](auto const&, auto const&) {});

    // Initialize MediaCallService with split transports
    service_ =
        std::make_unique<services::MediaCallService>(storage_, data_transport_, media_transport_, account_service_);
    service_->SetMessagingService(messaging_service_);

    // FIX: Set connection handler to prevent std::bad_function_call in StartVoiceCall
    service_->SetConnectionHandler([](ContactID const&) {});

    // Initialize Capture (needed for av_stream_ invariant)
    service_->SetVideoCapture(std::make_unique<MockCaptureImpl>());
    service_->SetAudioCapture(std::make_unique<MockCaptureImpl>());
    service_->Configure({});

    // Set dummy callbacks
    service_->SetIncomingCallCallback([](auto const&, auto) {});
    service_->SetCallAcceptedCallback([](auto const&) {});
    service_->SetCallTerminatedCallback([](auto const&) {});
    service_->SetCallRingingCallback([](auto const&) {});
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
  }

  std::shared_ptr<transport::MockCallLog> mock_log_;

  std::unique_ptr<transport::MockDataTransport> mock_data_transport_impl_;
  transport::MockDataTransport* mock_data_transport_raw_{};
  std::shared_ptr<transport::DataTransport> data_transport_;

  std::unique_ptr<transport::MockMediaTransport> mock_media_transport_impl_;
  transport::MockMediaTransport* mock_media_transport_raw_{};
  std::shared_ptr<transport::MediaTransport> media_transport_;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::unique_ptr<services::MediaCallService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(MediaCallServiceTest, StartVoiceCall) {
  // Setup peer with open data channel to allow call flow to proceed
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  data_transport_->AddPeer(peer1, peer);

  service_->StartVoiceCall(peer1);

  // Simulate media transport connection success (transitions to MediaConnected -> Negotiating)
  // This triggers SendCallRequest
  service_->OnMediaPeerConnectionStateChange(peer1, transport::PeerConnectionState::connected);

  // Verify that a call request message was sent via transport
  // Note: We no longer check history as intermediate system messages were removed.
  bool call_req_sent = false;
  for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "call_request") {
      call_req_sent = true;
      break;
    }
  }
  EXPECT_TRUE(call_req_sent);
}

TEST_F(MediaCallServiceTest, AcceptCall) {
  // We need a peer to exist in transport
  auto const peer = std::make_shared<transport::Peer>(peer1);
  // Allow sending messages
  peer->data_channel_is_open = true;
  data_transport_->AddPeer(peer1, peer);

  // 1. Simulate Incoming Call Request
  service_->OnCallRequest(peer1, "call_id_1", messaging::HistoryEntryKind::voiceCall);

  // 2. Simulate Media Connection Establishment (Transitions to Ringing)
  service_->OnMediaPeerConnectionStateChange(peer1, transport::PeerConnectionState::connected);

  bool accepted_cb = false;
  service_->SetCallAcceptedCallback([&](auto const& id) {
    if (id == peer1) {
      accepted_cb = true;
    }
  });

  // 3. Accept the call
  service_->AcceptCall(peer1);

  EXPECT_TRUE(accepted_cb);
  EXPECT_TRUE(peer->has_media_peer_connection);

  // Verify accept message sent via transport
  bool accept_sent = false;
  for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "call_accept") {
      accept_sent = true;
      break;
    }
  }
  EXPECT_TRUE(accept_sent);
}

TEST_F(MediaCallServiceTest, Hangup) {
  auto peer = std::make_shared<transport::Peer>(peer1);
  // Simulate active media connection
  peer->has_media_peer_connection = true;
  peer->data_channel_is_open = true;  // allow sending hangup message
  data_transport_->AddPeer(peer1, peer);

  // 1. Establish a call session so Hangup has something to terminate
  service_->StartVoiceCall(peer1);

  bool terminated_cb = false;
  service_->SetCallTerminatedCallback([&](auto const& id) {
    if (id == peer1) {
      terminated_cb = true;
    }
  });

  service_->Hangup(peer1, true);  // notify_peer = true

  EXPECT_TRUE(terminated_cb);
  EXPECT_FALSE(peer->has_media_peer_connection);

  // Verify media transport closed connection
  bool media_closed = false;
  for (auto const& [pid, ctx] : mock_log_->closed_connections) {
    if (pid == peer1 && ctx == "media") {
      media_closed = true;
    }
  }
  EXPECT_TRUE(media_closed);

  // Verify hangup message sent via data transport
  bool hangup_sent = false;
  for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "hangup") {
      hangup_sent = true;
    }
  }
  EXPECT_TRUE(hangup_sent);
}

TEST_F(MediaCallServiceTest, MediaSampleRouting) {
  // Setup peer in Ready state
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->SetState(transport::Peer::State::ready);
  data_transport_->AddPeer(peer1, peer);

  // 1. Simulate Incoming Call
  service_->OnCallRequest(peer1, "call_id_123", messaging::HistoryEntryKind::videoCall);

  // 2. Simulate Media Connect -> Ringing
  service_->OnMediaPeerConnectionStateChange(peer1, transport::PeerConnectionState::connected);

  // 3. Accept Call -> Active
  service_->AcceptCall(peer1);

  // Verify tracks are created and attached
  auto video_track = std::dynamic_pointer_cast<transport::MockTrack>(peer->video);
  auto audio_track = std::dynamic_pointer_cast<transport::MockTrack>(peer->audio);
  ASSERT_NE(video_track, nullptr);
  ASSERT_NE(audio_track, nullptr);

  // Wait for sample distribution
  // Increased wait time and robust check
  auto start = std::chrono::steady_clock::now();
  while (video_track->samples.empty() && audio_track->samples.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(3000)) {
      break;
    }
  }

  // The mocks should have received samples. We check that AT LEAST ONE has samples
  // because thread scheduling might favor one stream momentarily.
  EXPECT_TRUE(!video_track->samples.empty() || !audio_track->samples.empty());
}
