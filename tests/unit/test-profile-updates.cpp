#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class ProfileUpdatesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    client_ = std::make_shared<Client>(storage_, transport_);
    router_ = std::make_shared<view_model::ClientEventRouter>(client_);

    (void)client_->GetAccountService()->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    if (router_) router_->DetachFromClient();
    client_->Terminate();
  }

  void FlushClientQueue() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<view_model::ClientEventRouter> router_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

// Test that updating my profile triggers a broadcast to connected, confirmed peers.
TEST_F(ProfileUpdatesTest, BroadcastOnProfileUpdate) {
  // 1. Setup a confirmed, connected peer
  client_->AddContact(peer1, std::nullopt);
  // Simulate confirmation in storage
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->DebugReloadCache();  // Reload to pick up confirmed status

  // 2. Connect
  client_->Connect(peer1);
  FlushClientQueue();
  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  peer->data_channel_is_open = true;

  // 3. Update my profile
  mock_transport_->log.sent_peer_messages.clear();  // Clear history
  client_->UpdateMyProfile("New Name", "New Bio", "NewPhoto");
  FlushClientQueue();

  // 4. Verify "profile_info" message was sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "New Name");
      EXPECT_EQ(msg["bio"], "New Bio");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent) << "Profile update was not broadcast to connected peer";
}

// Test that profile info is sent immediately upon DataChannel open for confirmed contacts.
TEST_F(ProfileUpdatesTest, SendProfileOnConnect) {
  // 1. Setup a confirmed contact
  client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->DebugReloadCache();

  // Set my profile
  client_->UpdateMyProfile("My Name", "My Bio", "");

  // 2. Connect (simulate incoming or outgoing)
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Clear log before simulating open
  mock_transport_->log.sent_peer_messages.clear();

  // 3. Simulate DataChannel open by manipulating peer state and triggering logic?
  // Client::OnDataChannelOpen is called by transport delegate.
  // We can simulate it via the mock transport if we had access to the delegate pointer safely.
  // Since MockTransport has `delegate` public:
  ASSERT_TRUE(mock_transport_->delegate);

  // Trigger OnDataChannelOpen
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // 4. Verify profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "My Name");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent) << "Profile should be sent on DataChannel open";
}

// Test that profile is NOT sent on connect if contact is not confirmed (e.g. pending).
TEST_F(ProfileUpdatesTest, NoProfileSendForPendingContact) {
  // 1. Add contact but do NOT confirm (remains pendingApproval)
  client_->AddContact(peer1, std::nullopt);

  client_->Connect(peer1);
  FlushClientQueue();
  mock_transport_->log.sent_peer_messages.clear();

  // 2. Trigger OnDataChannelOpen
  ASSERT_TRUE(mock_transport_->delegate);
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // 3. Verify NO profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      profile_sent = true;
    }
  }
  EXPECT_FALSE(profile_sent) << "Profile should not be sent to unconfirmed contacts";
}

// Test that updating profile triggers connection attempt for disconnected confirmed contacts.
TEST_F(ProfileUpdatesTest, TriggerConnectForDisconnectedConfirmedPeer) {
  // 1. Add confirmed contact
  client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->DebugReloadCache();

  // 2. Ensure disconnected
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));
  mock_transport_->log.offers.clear();

  // 3. Update Profile
  client_->UpdateMyProfile("Conn Name", "Conn Bio", "NewPhoto");
  FlushClientQueue();

  // 4. Verify connection attempt
  // Connect() should have created the peer and sent an offer
  ASSERT_TRUE(mock_transport_->GetPeer(peer1));

  bool offer_sent = false;
  for (auto const& [pid, ctx] : mock_transport_->log.offers) {
    if (pid == peer1 && ctx == "data") offer_sent = true;
  }
  EXPECT_TRUE(offer_sent) << "Should attempt to connect to disconnected confirmed peer on profile update";
}

// Test ensuring the confirmer sends their profile immediately upon confirmation if connection is open.
TEST_F(ProfileUpdatesTest, ConfirmContactTriggersProfileSend) {
  client_->UpdateMyProfile("Approver", "Bio", "");

  // 1. Setup peer connection (e.g. request phase established connection)
  client_->Connect(peer1);
  FlushClientQueue();
  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  peer->data_channel_is_open = true;

  // Add contact in pending state (simulating request received)
  client_->AddContact(peer1, std::nullopt);  // starts pendingApproval, simulating request
  // Simulate receiving request to move to awaitingApproval
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::awaitingApproval});
  client_->GetContactService()->DebugReloadCache();

  mock_transport_->log.sent_peer_messages.clear();

  // 2. Confirm
  client_->ConfirmContact(peer1);
  FlushClientQueue();

  // 3. Check profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "Approver");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent);
}
