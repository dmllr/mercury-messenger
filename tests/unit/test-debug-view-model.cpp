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
  // 1. Add contact
  std::ignore = client_->AddContact(peer1, std::nullopt);
  std::ignore = client_->AddContact(peer2, std::nullopt);

  // 2. Confirm them manually in storage to simulate a confirmed state
  //    (since full handshake requires transport interaction which MockTransport doesn't fully auto-drive here)
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  mock_storage_->UpdateContact({peer2, identity::ContactStatus::confirmed});

  // 3. Force reload the cache in ContactManager so it sees the updates
  //    (We use the newly added DebugReloadCache logic or manually trigger it via restart)
  //    Since we can't easily restart in this test structure without losing `vm_`,
  //    we rely on the `DebugRevokeAllContactApprovals` calling `Reload` internally.
  //    BUT for the PRE-condition check, we need them to be confirmed.
  //    Let's use the new `DebugReloadCache` on the service if we can access it?
  //    We can't access `contact_service_->DebugReloadCache()` because it's on the service, not client interface public.
  //    Actually, `AddContact` added them as `pending`.
  //    We can use `client_->ConfirmContact(peer1)` but that expects `awaitingApproval`.
  //    Let's just re-create the client/vm to reload from storage for the setup phase?
  //    No, simpler: use `client_->GetContactService()->DebugReloadCache()` (casted or friend).
  //    Wait, `GetContactService()` returns shared_ptr to service. Service has `DebugReloadCache` public in the new
  //    header.
  client_->GetContactService()->ReloadCache();

  EXPECT_TRUE(client_->GetContactService()->IsConfirmed(peer1));
  EXPECT_TRUE(client_->GetContactService()->IsConfirmed(peer2));

  // Revoke
  vm_->RevokeAllApprovals();

  // Verify status change
  auto c1 = client_->GetContactService()->GetContact(peer1);
  auto c2 = client_->GetContactService()->GetContact(peer2);

  ASSERT_TRUE(c1);
  ASSERT_TRUE(c2);
  // MockPersistentStorage sets it to pendingApproval in DebugRevokeAllContactApprovals
  EXPECT_EQ(c1->status, identity::ContactStatus::pendingApproval);
  EXPECT_EQ(c2->status, identity::ContactStatus::pendingApproval);
}
