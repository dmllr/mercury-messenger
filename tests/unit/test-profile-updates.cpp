#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ProfileUpdatesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    client_ = std::make_shared<Client>(storage_, transport_, media_transport);

    (void)client_->GetAccountService()->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
  }

  void FlushClientQueue() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

// Test that updating my profile triggers a broadcast to connected, confirmed peers.
TEST_F(ProfileUpdatesTest, BroadcastOnProfileUpdate) {
  // Setup a confirmed, connected peer
  std::ignore = client_->AddContact(peer1, std::nullopt);

  // Directly update storage to confirm it, ensuring the rest of the fields are compliant
  // We use the storage interface directly to simulate the DB state change
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->ReloadCache();

  // Connect
  client_->Connect(peer1);
  FlushClientQueue();
  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  peer->data_channel_is_open = true;

  // Update my profile
  mock_transport_->CallLog()->sent_peer_messages.clear();  // Clear history
  client_->UpdateMyProfile("New Name", "New Bio", "NewPhoto");
  FlushClientQueue();

  // Verify "profile_info" message was sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
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
  // Setup a confirmed contact
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->ReloadCache();

  // Set my profile. This will queue a profile action for the confirmed contact
  client_->UpdateMyProfile("My Name", "My Bio", "");

  // Connect (simulate incoming or outgoing)
  client_->Connect(peer1);
  FlushClientQueue();

  auto const peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Clear log before simulating open
  mock_transport_->CallLog()->sent_peer_messages.clear();

  // Simulate DataChannel open
  ASSERT_TRUE(mock_transport_->delegate);
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // Verify profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "My Name");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent) << "Profile should be sent on DataChannel open";
}

// Test that profile is NOT sent on connect if contact is not confirmed (e.g. pending).
TEST_F(ProfileUpdatesTest, NoProfileSendForPendingContact) {
  // Add contact but do NOT confirm (remains pendingApproval)
  std::ignore = client_->AddContact(peer1, std::nullopt);

  client_->Connect(peer1);
  FlushClientQueue();
  mock_transport_->CallLog()->sent_peer_messages.clear();

  // Trigger OnDataChannelOpen
  ASSERT_TRUE(mock_transport_->delegate);
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // Verify NO profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      profile_sent = true;
    }
  }
  EXPECT_FALSE(profile_sent) << "Profile should not be sent to unconfirmed contacts";
}

// Test that updating profile triggers connection attempt for disconnected confirmed contacts.
TEST_F(ProfileUpdatesTest, TriggerConnectForDisconnectedConfirmedPeer) {
  // Add confirmed contact
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->GetContactService()->ReloadCache();

  // Ensure disconnected
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));
  mock_transport_->CallLog()->offers.clear();

  // Update Profile
  client_->UpdateMyProfile("Conn Name", "Conn Bio", "NewPhoto");
  FlushClientQueue();

  // Verify connection attempt
  // Connect() should have created the peer and sent an offer
  ASSERT_TRUE(mock_transport_->GetPeer(peer1));

  bool offer_sent = false;
  for (auto const& [pid, ctx] : mock_transport_->CallLog()->offers) {
    if (pid == peer1 && ctx == "data") {
      offer_sent = true;
    }
  }
  EXPECT_TRUE(offer_sent) << "Should attempt to connect to disconnected confirmed peer on profile update";
}

// Test ensuring the confirmer sends their profile immediately upon confirmation if connection is open.
TEST_F(ProfileUpdatesTest, ConfirmContactTriggersProfileSend) {
  client_->UpdateMyProfile("Approver", "Bio", "");

  // Setup peer connection (e.g. request phase established connection)
  client_->Connect(peer1);
  FlushClientQueue();
  auto const peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  peer->data_channel_is_open = true;

  // Add contact in pending state (simulating request received)
  std::ignore = client_->AddContact(peer1, std::nullopt);  // starts pendingApproval, simulating request
  // Simulate receiving request to move to awaitingApproval
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::awaitingApproval});
  client_->GetContactService()->ReloadCache();

  mock_transport_->CallLog()->sent_peer_messages.clear();

  // Confirm
  client_->ConfirmContact(peer1);
  FlushClientQueue();

  // Check profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "Approver");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent);
}
