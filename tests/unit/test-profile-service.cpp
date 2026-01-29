#include <gtest/gtest.h>

#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class ProfileServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_raw_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    account_service_ = std::make_unique<services::AccountService>(storage_, transport_);
    account_service_->SetOnMyProfileUpdatedCallback([]() {});
    std::ignore = account_service_->GenerateNewIdentity();
    account_service_->UpdateMyProfile("Test User", "", "");

    // Connection handler stub
    auto conn_handler = [](ContactID const&) {};

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

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

    profile_service_ = std::make_unique<services::ProfileService>(
        storage_,
        transport_,
        contact_manager,
        account_service_.get(),
        messaging_service_.get(),
        conn_handler
    );

    profile_service_->SetContactProfileUpdatedCallback([](auto const&) {});
    profile_service_->SetPeerListUpdateCallback([](auto const&) {});
    profile_service_->SetProfileReceivedCallback([](auto const&, auto const&) {});
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
  std::unique_ptr<services::ProfileService> profile_service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ProfileServiceTest, DeviceProfileReceived) {
  bool profile_cb_called = false;
  profile_service_->SetProfileReceivedCallback([&](ContactID const& id, Device::Profile const& profile) {
    if (id == peer1 && !profile.empty()) {
      EXPECT_EQ(profile[0].id, "0");
      EXPECT_EQ(profile[0].name, "Back Camera");
      profile_cb_called = true;
    }
  });

  nlohmann::json const profile_json = {
      {{"id", "0"}, {"name", "Back Camera"}, {"resolutions", {{{"width", 1280}, {"height", 720}}}}}
  };

  profile_service_->OnDeviceProfileReceived(peer1, profile_json);

  EXPECT_TRUE(profile_cb_called);
}

TEST_F(ProfileServiceTest, UpdateProfileTriggersSystemMessage) {
  bool update_cb_called = false;
  profile_service_->SetContactProfileUpdatedCallback([&](auto const& id) {
    if (id == peer1) update_cb_called = true;
  });

  nlohmann::json profile_data;
  profile_data["display_name"] = "New Name";

  profile_service_->OnProfileUpdate(peer1, profile_data);

  EXPECT_TRUE(update_cb_called);

  // Verify system message (diff) was generated
  auto history = messaging_service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  auto const& last_msg = history.messages[0];
  EXPECT_EQ(last_msg.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_NE(std::get<messaging::MessageEntry>(last_msg.data).content.find("profile_update_diff"), std::string::npos);
}
