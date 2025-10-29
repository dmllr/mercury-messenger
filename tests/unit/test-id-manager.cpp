#include "eillance/identity/id_manager.hpp"
#include "mock_persistent_storage.hpp"

#include <gtest/gtest.h>

using namespace eillance;

class IdManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_;
  std::shared_ptr<storage::PersistentStorage> storage_;
};

TEST_F(IdManagerTest, GeneratesNewIdentityWhenNoneExists) {
  // Initially, storage is empty.
  ASSERT_FALSE(storage_->GetSetting("identity.key").has_value());

  identity::IdManager id_manager(storage_);
  auto id = id_manager.GetId();

  EXPECT_FALSE(id.empty());
  auto pem_data_opt = storage_->GetSetting("identity.key");
  ASSERT_TRUE(pem_data_opt.has_value());

  // Verify the saved data can be loaded as an identity
  auto loaded_identity = crypto::Identity::LoadFromMemory(*pem_data_opt);
  ASSERT_NE(loaded_identity, nullptr);
  EXPECT_EQ(loaded_identity->GetId(), id);
}

TEST_F(IdManagerTest, LoadsExistingIdentity) {
  // Create an identity and save it to mock storage.
  auto identity = crypto::Identity::CreateNew();
  auto expected_id = identity->GetId();
  auto pem_data_str = identity->SaveToMemory();
  storage_->SetSetting("identity.key", pem_data_str);

  identity::IdManager id_manager(storage_);
  auto loaded_id = id_manager.GetId();

  EXPECT_EQ(loaded_id, expected_id);
}
