#include <gtest/gtest.h>

#include "eillance/client/settings_manager.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class SettingsManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    settings_manager_ = std::make_unique<SettingsManager>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<SettingsManager> settings_manager_;
};

TEST_F(SettingsManagerTest, GetDefaultUrl) {
  auto url = settings_manager_->GetSignalingServerUrl();
  EXPECT_FALSE(url.has_value());
}

TEST_F(SettingsManagerTest, SetAndGetUrl) {
  std::string custom_url = "ws://custom.server:8080";
  settings_manager_->SetSignalingServerUrl(custom_url);

  auto url = settings_manager_->GetSignalingServerUrl();
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url.value(), custom_url);
}

TEST_F(SettingsManagerTest, Persistence) {
  std::string custom_url = "ws://another.server:9000";
  settings_manager_->SetSignalingServerUrl(custom_url);

  // Create a new manager with the same storage to simulate app restart
  auto new_manager = std::make_unique<SettingsManager>(storage_);
  auto url = new_manager->GetSignalingServerUrl();

  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url.value(), custom_url);
}
