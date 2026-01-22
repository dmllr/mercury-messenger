#include <gtest/gtest.h>

#include "eillance/identity/id_manager.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class IdManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
};

TEST_F(IdManagerTest, NoIdentityExistsInitially) {
  identity::IdManager const id_manager(storage_);
  EXPECT_FALSE(id_manager.IdentityExists());
  EXPECT_TRUE(id_manager.GetPublicKey().empty());
}

TEST_F(IdManagerTest, GenerateNewIdentity) {
  // Initially, storage is empty.
  identity::IdManager id_manager(storage_);
  ASSERT_FALSE(id_manager.IdentityExists());

  std::string const mnemonic = id_manager.GenerateNewIdentity();
  EXPECT_FALSE(mnemonic.empty());
  EXPECT_TRUE(id_manager.IdentityExists());
  EXPECT_FALSE(id_manager.GetPublicKey().empty());

  // A new manager should load the created identity
  identity::IdManager const new_id_manager(storage_);
  EXPECT_TRUE(new_id_manager.IdentityExists());
  EXPECT_EQ(new_id_manager.GetPublicKey(), id_manager.GetPublicKey());
}

TEST_F(IdManagerTest, LoadsExistingIdentity) {
  // Create an identity and save it to mock storage.
  std::string mnemonic;
  auto identity = crypto::Identity::CreateNew(mnemonic);
  auto expected_id = identity->GetPublicKey();
  auto pem_data_str = identity->SaveToMemory();
  storage_->SetSetting("identity.key", pem_data_str);

  identity::IdManager const id_manager(storage_);
  auto loaded_id = id_manager.GetPublicKey();

  EXPECT_EQ(loaded_id, expected_id);
  EXPECT_TRUE(id_manager.IdentityExists());
}
