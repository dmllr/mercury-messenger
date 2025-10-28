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
  ASSERT_FALSE(mock_storage_impl_->Exists("identity.key"));

  identity::IdManager id_manager(storage_);
  auto id = id_manager.GetId();

  EXPECT_FALSE(id.empty());
  EXPECT_TRUE(mock_storage_impl_->Exists("identity.key"));

  // Verify the saved data can be loaded as an identity
  auto saved_data = mock_storage_impl_->Load("identity.key");
  ASSERT_TRUE(saved_data.has_value());
  std::string pem_str(reinterpret_cast<const char*>(saved_data->data()), saved_data->size());
  auto loaded_identity = crypto::Identity::LoadFromMemory(pem_str);
  ASSERT_NE(loaded_identity, nullptr);
  EXPECT_EQ(loaded_identity->GetId(), id);
}

TEST_F(IdManagerTest, LoadsExistingIdentity) {
  // Create an identity and save it to mock storage.
  auto identity = crypto::Identity::CreateNew();
  auto expected_id = identity->GetId();
  auto pem_data_str = identity->SaveToMemory();
  std::vector<std::byte> pem_data(reinterpret_cast<const std::byte*>(pem_data_str.data()), reinterpret_cast<const std::byte*>(pem_data_str.data()) + pem_data_str.size());
  mock_storage_impl_->Save("identity.key", pem_data);

  ASSERT_TRUE(mock_storage_impl_->Exists("identity.key"));

  identity::IdManager id_manager(storage_);
  auto loaded_id = id_manager.GetId();

  EXPECT_EQ(loaded_id, expected_id);
}
