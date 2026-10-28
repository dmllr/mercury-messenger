#include <filesystem>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/utils/event_queue.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class AccountServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    service_ = client_->Account();
  }

  void TearDown() override {
    service_.reset();
    client_->Terminate();
    client_.reset();
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<services::AccountService> service_;
};

TEST_F(AccountServiceTest, IdentityGeneration) {
  EXPECT_FALSE(service_->IdentityExists());

  std::string const mnemonic = service_->GenerateNewIdentity();
  EXPECT_FALSE(mnemonic.empty());
  EXPECT_TRUE(service_->IdentityExists());

  auto const id = service_->GetId();
  EXPECT_FALSE(id.IsEmpty());

  auto const stored_mnemonic = service_->GetMnemonic();
  ASSERT_TRUE(stored_mnemonic.has_value());
  EXPECT_EQ(*stored_mnemonic, mnemonic);
}

TEST_F(AccountServiceTest, RestoreIdentity) {
  std::string original_mnemonic;
  {
    auto temp_id = crypto::Identity::CreateNew(original_mnemonic);
  }

  EXPECT_TRUE(service_->RestoreIdentity(original_mnemonic));
  EXPECT_TRUE(service_->IdentityExists());

  auto stored_mnemonic = service_->GetMnemonic();
  ASSERT_TRUE(stored_mnemonic.has_value());
  EXPECT_EQ(*stored_mnemonic, original_mnemonic);
}

TEST_F(AccountServiceTest, UpdateProfile) {
  bool callback_called = false;
  auto const sub =
      EventQueue::Instance().Subscribe<EventMyProfileUpdated>([&](auto const&) -> void { callback_called = true; });

  service_->UpdateMyProfile({.display_name = "Alice", .bio = "Hello World", .photo_base64 = "base64photo"});

  EventQueue::Instance().WaitFor([&] { return callback_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  auto const* settings = service_->GetSettings();
  EXPECT_EQ(settings->GetMyDisplayName(), "Alice");
  EXPECT_EQ(settings->GetMyBio(), "Hello World");
  EXPECT_EQ(settings->GetMyPhotoBase64(), "base64photo");
  EXPECT_TRUE(callback_called);
}

TEST_F(AccountServiceTest, UpdateBootstrapNodeUrl) {
  std::string const new_url = "/ip4/127.0.0.1/tcp/1234";
  service_->UpdateBootstrapNodeUrls(new_url, false);

  EXPECT_EQ(service_->GetSettings()->GetBootstrapNodeUrls(), new_url);

  // Verify reconnection triggering implicitly
  EXPECT_TRUE(client_->GetSettingsManager()->GetBootstrapNodeUrls() == new_url);
}

TEST_F(AccountServiceTest, Signing) {
  std::ignore = service_->GenerateNewIdentity();

  std::string message = "test message";
  std::vector<std::byte> msg_bytes(message.size());
  std::ranges::transform(message, msg_bytes.begin(), [](char c) -> std::byte { return static_cast<std::byte>(c); });

  auto const signature = service_->Sign(msg_bytes);
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
