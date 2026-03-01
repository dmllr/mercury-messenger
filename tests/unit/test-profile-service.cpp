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

class ProfileServiceTest : public ::testing::Test {
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
  std::unique_ptr<services::ServiceManager> sm_;
  std::shared_ptr<services::ProfileService> profile_service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ProfileServiceTest, UpdateProfileTriggersSystemMessage) {
  bool update_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventContactProfileUpdated>([&](auto const& e) {
    if (e.peer_id == peer1) {
      update_cb_called = true;
    }
  });

  nlohmann::json profile_data;
  profile_data["display_name"] = "New Name";

  profile_service_->OnProfileUpdate(peer1, profile_data);

  auto const start = std::chrono::steady_clock::now();
  while (!update_cb_called && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EventQueue::Instance().Unsubscribe<EventContactProfileUpdated>(sub);
  EXPECT_TRUE(update_cb_called);

  // Verify system message (diff) was generated
  auto history = messaging_service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  auto const& last_msg = history.messages[0];
  EXPECT_EQ(last_msg.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_NE(std::get<messaging::MessageEntry>(last_msg.data).content.find("profile_update_diff"), std::string::npos);
}
