#include <thread>

#include <gtest/gtest.h>

#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ContactServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_raw_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    sm_ = std::make_unique<services::ServiceManager>();
    sm_->storage = storage_;
    sm_->data_transport = transport_;
    sm_->contact_manager = std::make_shared<identity::ContactManager>(storage_);

    account_service_ = std::make_shared<services::AccountService>(sm_.get());
    sm_->account = account_service_;

    std::ignore = account_service_->GenerateNewIdentity();
    account_service_->UpdateMyProfile({.display_name = "Test User", .bio = "", .photo_base64 = ""});

    file_transfer_service_ = std::make_shared<services::FileTransferService>(sm_.get());
    sm_->file_transfer = file_transfer_service_;

    messaging_service_ = std::make_shared<services::MessagingService>(sm_.get());
    sm_->messaging = messaging_service_;

    profile_service_ = std::make_shared<services::ProfileService>(sm_.get());
    sm_->profile = profile_service_;

    contact_service_ = std::make_shared<services::ContactService>(sm_.get());
    sm_->contact = contact_service_;
  }

  void TearDown() override {
    file_transfer_service_.reset();
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_raw_{};
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::ProfileService> profile_service_;
  std::unique_ptr<services::ServiceManager> sm_;
  std::shared_ptr<services::ContactService> contact_service_;
  uint64_t conn_sub_id_ = 0;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ContactServiceTest, CannotAddSelf) {
  auto const my_id = account_service_->GetId();

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
  auto [messages, total_messages] = messaging_service_->GetHistoryPage(peer1, 0, 10);
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
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& msg : mock_transport_raw_->CallLog()->sent_signaling_messages) {
          if (msg.value("type", "") == "contact_confirm" && msg.value("to", "") == peer1.ToString()) {
            return true;
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  // Check that confirmation messages were queued/sent
  // "contact_confirm" -> sent via signaling
  bool signaling_sent = false;
  for (auto const& msg : mock_transport_raw_->CallLog()->sent_signaling_messages) {
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
  for (auto const& pid : mock_transport_raw_->CallLog()->closed_connections | std::views::keys) {
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
