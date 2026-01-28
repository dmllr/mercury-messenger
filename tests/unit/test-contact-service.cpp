#include <gtest/gtest.h>

#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class ContactServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_raw_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    account_service_ = std::make_unique<services::AccountService>(storage_, transport_);
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    account_service_->GenerateNewIdentity();
    account_service_->UpdateMyProfile("Test User", "", "");

    // Connection handler stub that forces message queue flushing
    auto conn_handler = [this](ContactID const& id) {
      if (messaging_service_) {
        messaging_service_->SendQueuedMessages(id);
      }
    };

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

    contact_service_ = std::make_unique<services::ContactService>(
        storage_,
        transport_,
        contact_manager,
        account_service_.get(),
        conn_handler
    );

    // Initialize dummy callbacks to avoid invariants
    contact_service_->SetContactRequestCallback([](auto const&) {});
    contact_service_->SetContactStatusChangedCallback([](auto const&, auto) {});
    contact_service_->SetContactProfileUpdatedCallback([](auto const&) {});
    contact_service_->SetContactRemovedCallback([](auto const&) {});
    contact_service_->SetPeerListUpdateCallback([](auto const&) {});
    contact_service_->SetProfileReceivedCallback([](auto const&, auto const&) {});

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    messaging_service_ = std::make_unique<services::MessagingService>(
        storage_,
        transport_,
        contact_manager,
        file_transfer_service_,
        conn_handler
    );

    // Initialize dummy callbacks for MessagingService
    messaging_service_->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    messaging_service_->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});

    contact_service_->SetMessagingService(messaging_service_.get());
  }

  void TearDown() override {
      file_transfer_service_.reset();
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_raw_{};
  std::shared_ptr<transport::Transport> transport_;
  std::unique_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::unique_ptr<services::MessagingService> messaging_service_;
  std::unique_ptr<services::ContactService> contact_service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ContactServiceTest, AddContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::pendingApproval);

  // Verify system message was generated
  auto history = messaging_service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  auto const& last_msg = history.messages[0];
  EXPECT_EQ(last_msg.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_EQ(last_msg.direction, messaging::MessageDirection::sent);
}

TEST_F(ContactServiceTest, ReceiveContactRequest) {
  bool request_cb_called = false;
  contact_service_->SetContactRequestCallback([&](auto const& id) {
    if (id == peer1) request_cb_called = true;
  });

  nlohmann::json profile;
  profile["display_name"] = "Peer One";

  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile);

  EXPECT_TRUE(request_cb_called);
  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::awaitingApproval);
  EXPECT_EQ(contact->display_name, "Peer One");
}

TEST_F(ContactServiceTest, ConfirmContact) {
  // First receive a request
  nlohmann::json profile;
  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile);

  bool status_cb_called = false;
  contact_service_->SetContactStatusChangedCallback([&](auto const& id, auto status) {
    if (id == peer1 && status == identity::ContactStatus::confirmed) status_cb_called = true;
  });

  contact_service_->ConfirmContact(peer1);

  EXPECT_TRUE(status_cb_called);
  EXPECT_TRUE(contact_service_->IsConfirmed(peer1));

  // Check that confirmation messages were queued/sent
  // "contact_confirm" -> sent via signaling
  bool signaling_sent = false;
  for (auto const& msg : mock_transport_raw_->log.sent_signaling_messages) {
    if (msg["type"] == "contact_confirm" && msg["to"] == peer1.ToString()) {
      signaling_sent = true;
      break;
    }
  }
  EXPECT_TRUE(signaling_sent);
}

TEST_F(ContactServiceTest, BlockContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  // Setup peer in transport to verify connection closing
  mock_transport_raw_->AddPeer(peer1, std::make_shared<transport::Peer>(peer1));

  contact_service_->BlockContact(peer1, true);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::rejected);

  // Check if connections were closed
  bool closed = false;
  for (auto const& [pid, ctx] : mock_transport_raw_->log.closed_connections) {
    if (pid == peer1) closed = true;
  }
  EXPECT_TRUE(closed);
}

TEST_F(ContactServiceTest, UnblockContact) {
  contact_service_->AddContact(peer1, std::nullopt);
  contact_service_->BlockContact(peer1, false);

  contact_service_->UnblockContact(peer1);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  // Should revert to awaiting approval or similar non-blocked state
  EXPECT_NE(contact->status, identity::ContactStatus::rejected);
}

TEST_F(ContactServiceTest, DeviceProfileReceived) {
  bool profile_cb_called = false;
  contact_service_->SetProfileReceivedCallback([&](ContactID const& id, Device::Profile const& profile) {
    if (id == peer1 && !profile.empty()) {
      EXPECT_EQ(profile[0].id, "0");
      EXPECT_EQ(profile[0].name, "Back Camera");
      profile_cb_called = true;
    }
  });

  nlohmann::json const profile_json = {
      {{"id", "0"}, {"name", "Back Camera"}, {"resolutions", {{{"width", 1280}, {"height", 720}}}}}
  };

  contact_service_->OnDeviceProfileReceived(peer1, profile_json);

  EXPECT_TRUE(profile_cb_called);
}
