#include "eillance/view_model/debug_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class DebugViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<DebugViewModel>(client_, router_);
  }

  std::shared_ptr<DebugViewModel> vm_;
};

TEST_F(DebugViewModelTest, EraseAllHistory) {
  // Add some history
  client_->GetMessagingService()->AddSystemEntry(peer1, "sys", messaging::MessageDirection::received);
  client_->GetMessagingService()->AddSystemEntry(peer2, "sys", messaging::MessageDirection::received);

  // Verify addition
  EXPECT_FALSE(client_->GetMessageHistoryPage(peer1, 0, 10).messages.empty());
  EXPECT_FALSE(client_->GetMessageHistoryPage(peer2, 0, 10).messages.empty());

  // Erase
  vm_->EraseAllHistory();

  // Verify deletion
  EXPECT_TRUE(client_->GetMessageHistoryPage(peer1, 0, 10).messages.empty());
  EXPECT_TRUE(client_->GetMessageHistoryPage(peer2, 0, 10).messages.empty());
}

TEST_F(DebugViewModelTest, EraseAllContacts) {
  // Add contacts via Client API so ContactManager is aware
  std::ignore = client_->AddContact(peer1, std::nullopt);
  std::ignore = client_->AddContact(peer2, std::nullopt);

  // Wait for async add to propagate
  WaitFor([&] { return !client_->GetContacts().empty(); });

  // Verify addition
  EXPECT_FALSE(client_->GetContacts().empty());

  // Erase
  vm_->EraseAllContacts();

  // Verify deletion
  EXPECT_TRUE(client_->GetContacts().empty());
}

TEST_F(DebugViewModelTest, RevokeAllApprovals) {
  // Set up confirmed contacts properly via Client/Service API

  // Add contact
  std::ignore = client_->AddContact(peer1, std::nullopt);
  std::ignore = client_->AddContact(peer2, std::nullopt);

  // Confirm them manually in storage to simulate a confirmed state
  // (since full handshake requires transport interaction which MockTransport doesn't fully auto-drive here)
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  mock_storage_->UpdateContact({peer2, identity::ContactStatus::confirmed});

  // Force reload the cache in ContactManager so it sees the updates
  client_->GetContactService()->ReloadCache();

  EXPECT_TRUE(client_->GetContactService()->IsConfirmed(peer1));
  EXPECT_TRUE(client_->GetContactService()->IsConfirmed(peer2));

  // Revoke
  vm_->RevokeAllApprovals();

  // Verify status change
  auto contact1 = client_->GetContactService()->GetContact(peer1);
  auto contact2 = client_->GetContactService()->GetContact(peer2);

  ASSERT_TRUE(contact1);
  ASSERT_TRUE(contact2);
  // MockPersistentStorage sets it to pendingApproval in DebugRevokeAllContactApprovals
  EXPECT_EQ(contact1->status, identity::ContactStatus::pendingApproval);
  EXPECT_EQ(contact2->status, identity::ContactStatus::pendingApproval);
}
