#include <filesystem>

#include <gtest/gtest.h>

#include "eillance/services/account_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class AccountServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_data_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_data_transport_raw_ = mock_data_transport_impl_.get();

    data_transport_ = std::make_shared<transport::DataTransport>(std::move(mock_data_transport_impl_));

    service_ = std::make_unique<services::AccountService>(storage_, data_transport_);

    // Set callback to prevent invariant violations during identity generation/update
    service_->SetOnMyProfileUpdatedCallback([]() {});
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_data_transport_impl_;
  transport::MockDataTransport* mock_data_transport_raw_{};
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::unique_ptr<services::AccountService> service_;
};

TEST_F(AccountServiceTest, IdentityGeneration) {
  EXPECT_FALSE(service_->IdentityExists());

  std::string mnemonic = service_->GenerateNewIdentity();
  EXPECT_FALSE(mnemonic.empty());
  EXPECT_TRUE(service_->IdentityExists());

  auto id = service_->GetId();
  EXPECT_FALSE(id.IsEmpty());

  auto stored_mnemonic = service_->GetMnemonic();
  ASSERT_TRUE(stored_mnemonic.has_value());
  EXPECT_EQ(*stored_mnemonic, mnemonic);
}

TEST_F(AccountServiceTest, RestoreIdentity) {
  std::string original_mnemonic;
  { auto temp_id = crypto::Identity::CreateNew(original_mnemonic); }

  EXPECT_TRUE(service_->RestoreIdentity(original_mnemonic));
  EXPECT_TRUE(service_->IdentityExists());

  auto stored_mnemonic = service_->GetMnemonic();
  ASSERT_TRUE(stored_mnemonic.has_value());
  EXPECT_EQ(*stored_mnemonic, original_mnemonic);
}

TEST_F(AccountServiceTest, UpdateProfile) {
  bool callback_called = false;
  service_->SetOnMyProfileUpdatedCallback([&] { callback_called = true; });

  service_->UpdateMyProfile("Alice", "Hello World", "base64photo");

  auto* settings = service_->GetSettings();
  EXPECT_EQ(settings->GetMyDisplayName(), "Alice");
  EXPECT_EQ(settings->GetMyBio(), "Hello World");
  EXPECT_EQ(settings->GetMyPhotoBase64(), "base64photo");
  EXPECT_TRUE(callback_called);
}

TEST_F(AccountServiceTest, UpdateSignalingUrl) {
  std::string const new_url = "wss://test.server.com";
  service_->UpdateSignalingUrl(new_url);

  EXPECT_EQ(service_->GetSettings()->GetSignalingServerUrl(), new_url);

  ASSERT_EQ(mock_log_->signaling_urls.size(), 1);
  EXPECT_EQ(mock_log_->signaling_urls[0], new_url);

  ASSERT_EQ(mock_log_->starts.size(), 1);
  EXPECT_EQ(mock_log_->starts[0], "Reconnect");
}

TEST_F(AccountServiceTest, Signing) {
  std::ignore = service_->GenerateNewIdentity();

  std::string message = "test message";
  std::vector<std::byte> msg_bytes(message.size());
  std::ranges::transform(message, msg_bytes.begin(), [](char c) { return std::byte(c); });

  auto signature = service_->Sign(msg_bytes);
  EXPECT_FALSE(signature.empty());
}

TEST_F(AccountServiceTest, RestoreBackupLifecycle) {
  std::string const backup_path = "test_backup.mcr";
  std::string const password = "password";

  std::ignore = service_->GenerateNewIdentity();

  {
    std::ofstream db_file(storage_->GetPath(), std::ios::binary);
    db_file << "dummy_content";
  }

  ASSERT_TRUE(service_->CreateBackup(backup_path, password));
  EXPECT_TRUE(service_->RestoreBackup(backup_path, password));

  std::filesystem::remove(backup_path);
  std::filesystem::remove(storage_->GetPath());
}
