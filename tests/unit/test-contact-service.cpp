#include <thread>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/sync_engine.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ContactServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport = std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->UpdateMyProfile({.display_name = "Test User", .bio = "", .photo_base64 = ""});
    
    contact_service_ = client_->Contacts();
  }

  void TearDown() override {
    contact_service_.reset();
    client_->Terminate();
    client_.reset();
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<services::ContactService> contact_service_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(ContactServiceTest, CannotAddSelf) {
  auto const my_id = client_->GetId();

  auto const result = contact_service_->AddContact(my_id, std::nullopt);
  EXPECT_FALSE(result);

  auto const contact = contact_service_->GetContact(my_id);
  EXPECT_FALSE(contact.has_value());
}

TEST_F(ContactServiceTest, AddContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  auto const contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::pendingApproval);

  // Verify system message was generated
  auto [messages, total_messages] = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(messages.empty());
  auto const& last_msg = messages[0];
  EXPECT_EQ(last_msg.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_EQ(last_msg.direction, messaging::MessageDirection::sent);
}

TEST_F(ContactServiceTest, ReceiveContactRequest) {
  bool request_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventContactRequest>([&](auto const& e) {
    if (e.peer_id == peer1) {
      request_cb_called = true;
    }
  });

  nlohmann::json profile;
  profile["display_name"] = "Peer One";

  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile);

  EventQueue::Instance().WaitFor([&] { return request_cb_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(request_cb_called);
  auto const contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::awaitingApproval);
  EXPECT_EQ(contact->display_name, "Peer One");
}

TEST_F(ContactServiceTest, ConfirmContact) {
  // First receive a request
  nlohmann::json const profile;
  contact_service_->OnContactAddRequest(peer1, std::nullopt, profile);

  bool status_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventContactStatusChanged>([&](auto const& e) {
    if (e.peer_id == peer1 && e.status == identity::ContactStatus::confirmed) {
      status_cb_called = true;
    }
  });

  contact_service_->ConfirmContact(peer1);

  EventQueue::Instance().WaitFor([&] { return status_cb_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(status_cb_called);
  EXPECT_TRUE(contact_service_->IsConfirmed(peer1));

  EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& entry : entries) {
          if (entry.type == storage::LedgerActionType::ContactState &&
              entry.payload.find("contact_confirm") != std::string::npos)
            return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool signaling_sent = false;
  for (auto const& entry : storage_->GetLedgerEntries(TopicID(peer1))) {
    if (entry.type == storage::LedgerActionType::ContactState &&
        entry.payload.find("contact_confirm") != std::string::npos) {
      signaling_sent = true;
      break;
    }
  }
  EXPECT_TRUE(signaling_sent);
}

TEST_F(ContactServiceTest, BlockContact) {
  contact_service_->AddContact(peer1, std::nullopt);

  contact_service_->BlockContact(peer1, true);

  auto contact = contact_service_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::rejected);

  // Check if connections were closed - Media transport will receive Disconnect
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  bool closed = false;
  for (auto const& [pid, ctx] : mock_log_->closed_connections) {
    if (pid == peer1) {
      closed = true;
    }
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
