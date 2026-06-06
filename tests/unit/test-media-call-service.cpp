#include <gtest/gtest.h>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/sync_engine.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class MediaCallServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Shared Log
    mock_log_ = std::make_shared<transport::MockCallLog>();

    // Setup Media Transport
    mock_media_transport_impl_ = std::make_unique<transport::MockMediaTransport>(mock_log_);
    mock_media_transport_raw_ = mock_media_transport_impl_.get();
    media_transport_ = std::make_shared<transport::MediaTransport>(std::move(mock_media_transport_impl_));

    // Setup messaging dependencies
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport_, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));
  }

  std::shared_ptr<transport::MockCallLog> mock_log_;

  std::unique_ptr<transport::MockMediaTransport> mock_media_transport_impl_;
  transport::MockMediaTransport* mock_media_transport_raw_{};
  std::shared_ptr<transport::MediaTransport> media_transport_;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(MediaCallServiceTest, StartVoiceCall) {
  bool outgoing_triggered = false;
  auto sub = EventQueue::Instance().Subscribe<EventOutgoingCall>([&](auto const& e) {
    if (e.peer_id == peer1) outgoing_triggered = true;
  });

  client_->StartVoiceCall(peer1);

  EventQueue::Instance().WaitFor([&] { return outgoing_triggered; }, std::chrono::milliseconds(2000));
  EXPECT_TRUE(outgoing_triggered);
  EventQueue::Instance().Unsubscribe(sub);
}

TEST_F(MediaCallServiceTest, AcceptCall) {
  // 1. Simulate Incoming Call Request
  client_->Calls()->OnCallRequest(peer1, peer1, "call_id_1", messaging::HistoryEntryKind::voiceCall);
  client_->Calls()->OnCallRinging(peer1, "call_id_1");
  // Also push media state connected to hit the correct accept logic
  EventQueue::Instance().Push(EventMediaPeerConnectionStateChanged{peer1, transport::PeerConnectionState::connected});

  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  bool accepted_cb = false;
  auto sub = EventQueue::Instance().Subscribe<EventCallAccepted>([&](auto const& e) {
    if (e.peer_id == peer1) {
      accepted_cb = true;
    }
  });

  // 3. Accept the call
  client_->AcceptCall(peer1);

  EventQueue::Instance().WaitFor([&] { return accepted_cb; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(accepted_cb);
}

TEST_F(MediaCallServiceTest, Hangup) {
  // 1. Establish a call session so Hangup has something to terminate
  client_->StartVoiceCall(peer1);

  bool terminated_cb = false;
  auto sub = EventQueue::Instance().Subscribe<EventCallTerminated>([&](auto const& e) {
    if (e.peer_id == peer1) {
      terminated_cb = true;
    }
  });

  client_->Hangup(peer1, true);  // notify_peer = true

  EventQueue::Instance().WaitFor([&] { return terminated_cb; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(terminated_cb);

  // Give the event queue and dispatch queues time to flush
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  // Verify media transport closed connection
  bool media_closed = false;
  for (auto const& [pid, ctx] : mock_log_->closed_connections) {
    if (pid == peer1 && ctx == "media") {
      media_closed = true;
    }
  }
  EXPECT_TRUE(media_closed);
}

TEST_F(MediaCallServiceTest, RejectCall) {
  EventQueue::Instance().Push(EventIncomingCall{peer1, messaging::HistoryEntryKind::voiceCall});
  // Setup to ensure it's loaded as current_call
  client_->Calls()->OnCallRequest(peer1, peer1, "call_id_1", messaging::HistoryEntryKind::voiceCall);

  bool terminated_cb = false;
  auto sub = EventQueue::Instance().Subscribe<EventCallTerminated>([&](auto const& e) {
    if (e.peer_id == peer1) {
      terminated_cb = true;
    }
  });

  client_->RejectCall(peer1);

  EventQueue::Instance().WaitFor([&] { return terminated_cb; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(terminated_cb);
}
