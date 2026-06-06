#include <optional>

#include <gtest/gtest.h>

#include "eillance/identity/group_manager.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class GroupManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    group_manager_ = std::make_unique<identity::GroupManager>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::GroupManager> group_manager_;
};

TEST_F(GroupManagerTest, CreateGroup) {
  auto const group_id = identity::GroupManager::GenerateGroupID();
  identity::Group g;
  g.id = group_id;
  g.name = "Test Group";
  g.owner_id = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

  group_manager_->CreateGroupLocal(g);

  auto fetched = group_manager_->GetGroup(group_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->name, "Test Group");
  EXPECT_EQ(fetched->owner_id, g.owner_id);
}

TEST_F(GroupManagerTest, AddRemoveMembers) {
  auto const group_id = identity::GroupManager::GenerateGroupID();
  identity::Group g;
  g.id = group_id;
  g.name = "Test Group";
  g.owner_id = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  group_manager_->CreateGroupLocal(g);

  ContactID new_member("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15");
  group_manager_->AddMemberLocal(group_id, new_member, 0);

  auto fetched = group_manager_->GetGroup(group_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->members.size(), 1);
  EXPECT_EQ(fetched->members[0].contact_id, new_member);

  group_manager_->RemoveMemberLocal(group_id, new_member);
  fetched = group_manager_->GetGroup(group_id);
  EXPECT_TRUE(fetched->members.empty());
}
