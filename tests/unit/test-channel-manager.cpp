#include <optional>

#include <gtest/gtest.h>

#include "eillance/identity/channel_manager.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ChannelManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    channel_manager_ = std::make_unique<identity::ChannelManager>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::ChannelManager> channel_manager_;
};

TEST_F(ChannelManagerTest, CreateChannel) {
  auto channel_id = identity::ChannelManager::GenerateChannelID();
  identity::Channel c;
  c.id = channel_id;
  c.name = "Test Channel";
  c.owner_id = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

  channel_manager_->CreateChannelLocal(c);

  auto fetched = channel_manager_->GetChannel(channel_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->name, "Test Channel");
  EXPECT_EQ(fetched->owner_id, c.owner_id);
}

TEST_F(ChannelManagerTest, AddRemoveMembers) {
  auto channel_id = identity::ChannelManager::GenerateChannelID();
  identity::Channel c;
  c.id = channel_id;
  c.name = "Test Channel";
  c.owner_id = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  channel_manager_->CreateChannelLocal(c);

  ContactID new_member("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15");
  channel_manager_->AddMemberLocal(channel_id, new_member, 0);

  auto fetched = channel_manager_->GetChannel(channel_id);
  ASSERT_TRUE(fetched.has_value());
  EXPECT_EQ(fetched->members.size(), 1);
  EXPECT_EQ(fetched->members[0].contact_id, new_member);

  channel_manager_->RemoveMemberLocal(channel_id, new_member);
  fetched = channel_manager_->GetChannel(channel_id);
  EXPECT_TRUE(fetched->members.empty());
}
