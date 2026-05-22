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

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);

    (void)client_->Account()->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

// Test that updating my profile triggers a broadcast to connected, confirmed peers.
TEST_F(ProfileUpdatesTest, BroadcastOnProfileUpdate) {
  // Setup a confirmed, connected peer
  std::ignore = client_->AddContact(peer1, std::nullopt);

  // Directly update storage to confirm it, ensuring the rest of the fields are compliant
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  // Update my profile
  client_->UpdateMyProfile({.display_name = "New Name", .bio = "New Bio", .photo_base64 = "NewPhoto"});

  // Verify "profile_info" message was appended to the ledger
  bool profile_sent = false;
  for (auto const& entry : storage_->GetLedgerEntries(TopicID(peer1))) {
    if (entry.type == messaging::LedgerActionType::ProfileUpdate) {
      auto json = nlohmann::json::parse(entry.payload);
      EXPECT_EQ(json["display_name"], "New Name");
      EXPECT_EQ(json["bio"], "New Bio");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent) << "Profile update was not broadcast to connected peer";
}

// Test that profile info is queued up for sync on connect for confirmed contacts.
TEST_F(ProfileUpdatesTest, SendProfileOnConnect) {
  // Setup a confirmed contact
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  // Set my profile. This will queue a profile action for the confirmed contact
  client_->UpdateMyProfile({.display_name = "My Name", .bio = "My Bio", .photo_base64 = ""});

  auto reqs = storage_->GetSyncRequests();
  bool found = false;
  for (auto const& r : reqs) {
    if (r.target_peer_id == peer1) {
      found = true;
    }
  }
  EXPECT_TRUE(found) << "Sync request should be enqueued when profile updates";
}

// Test that profile is NOT sent on connect if contact is not confirmed (e.g. pending).
TEST_F(ProfileUpdatesTest, NoProfileSendForPendingContact) {
  // Add contact but do NOT confirm (remains pendingApproval)
  std::ignore = client_->AddContact(peer1, std::nullopt);

  // Update my profile
  client_->UpdateMyProfile({.display_name = "New Name", .bio = "New Bio", .photo_base64 = "NewPhoto"});

  // Verify NO profile sent to peer1 in ledger
  bool profile_sent = false;
  for (auto const& entry : storage_->GetLedgerEntries(TopicID(peer1))) {
    if (entry.type == messaging::LedgerActionType::ProfileUpdate) {
      profile_sent = true;
    }
  }
  EXPECT_FALSE(profile_sent) << "Profile should not be sent to unconfirmed contacts";
}

// Test that updating profile triggers connection attempt for disconnected confirmed contacts.
TEST_F(ProfileUpdatesTest, TriggerConnectForDisconnectedConfirmedPeer) {
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  // Ensure any previous events processed
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  // Update Profile
  client_->UpdateMyProfile({.display_name = "Conn Name", .bio = "Conn Bio", .photo_base64 = "NewPhoto"});

  eillance::EventQueue::Instance().WaitFor(
      [&]() { return client_->GetPeerConnectionState(peer1) == transport::PeerConnectionState::connecting; },
      std::chrono::milliseconds(2000)
  );

  EXPECT_EQ(client_->GetPeerConnectionState(peer1), transport::PeerConnectionState::connecting)
      << "Should attempt to connect to disconnected confirmed peer on profile update";
}

// Test ensuring the confirmer sends their profile immediately upon confirmation if connection is open.
TEST_F(ProfileUpdatesTest, ConfirmContactTriggersProfileSend) {
  client_->UpdateMyProfile({.display_name = "Approver", .bio = "Bio", .photo_base64 = ""});

  std::ignore = client_->AddContact(peer1, std::nullopt);  // starts pendingApproval, simulating request
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::awaitingApproval});
  client_->Contacts()->ReloadCache();

  // Confirm
  client_->ConfirmContact(peer1);
  bool profile_sent = false;
  eillance::EventQueue::Instance().WaitFor(
      [&]() -> bool {
        for (auto const& entry : storage_->GetLedgerEntries(TopicID(peer1))) {
          if (entry.type == messaging::LedgerActionType::ProfileUpdate) {
            auto json = nlohmann::json::parse(entry.payload);
            if (json.value("type", "") == "profile_info") {
              profile_sent = true;
              return true;
            }
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  EXPECT_TRUE(profile_sent);
}
