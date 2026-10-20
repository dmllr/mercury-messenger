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

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<SettingsManager> settings_manager_;
};

TEST_F(SettingsManagerTest, GetDefaultUrl) {
  auto const urls = settings_manager_->GetBootstrapNodeUrls();
  EXPECT_FALSE(urls.has_value());
  auto const default_urls = settings_manager_->GetEffectiveBootstrapNodeUrls();
  EXPECT_EQ(default_urls.size(), 4);
}

TEST_F(SettingsManagerTest, SetAndGetUrl) {
  std::string const custom_url = "/ip4/127.0.0.1/tcp/8080";
  settings_manager_->SetBootstrapNodeUrls(custom_url, false);

  auto const url = settings_manager_->GetBootstrapNodeUrls();
  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url.value(), custom_url);
}

TEST_F(SettingsManagerTest, Persistence) {
  std::string const custom_url = "/ip4/192.168.1.1/tcp/9000";
  settings_manager_->SetBootstrapNodeUrls(custom_url, false);

  // Create a new manager with the same storage to simulate app restart
  auto const new_manager = std::make_unique<SettingsManager>(storage_);
  auto const url = new_manager->GetBootstrapNodeUrls();

  ASSERT_TRUE(url.has_value());
  EXPECT_EQ(url.value(), custom_url);
}
