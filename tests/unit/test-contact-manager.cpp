#include "eillance/identity/contact_manager.hpp"
#include "mock_persistent_storage.hpp"

#include <gtest/gtest.h>
#include <optional>

using namespace eillance;

class ContactManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    contact_manager_ = std::make_unique<identity::ContactManager>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::ContactManager> contact_manager_;
  const ContactID peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  const ContactID peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};

TEST_F(ContactManagerTest, AddContact) {
  ASSERT_TRUE(contact_manager_->AddContact(peer1));
  auto contact = contact_manager_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->peer_id, peer1);
  EXPECT_EQ(contact->status, identity::ContactStatus::pendingApproval);
}

TEST_F(ContactManagerTest, AddExistingContact) {
  contact_manager_->AddContact(peer1);
  contact_manager_->MarkAsConfirmed(peer1);  // Change status away from pending
  ASSERT_FALSE(contact_manager_->AddContact(peer1));
}

TEST_F(ContactManagerTest, ReceiveContactRequest) {
  EXPECT_EQ(contact_manager_->ReceiveContactRequest(peer1, std::nullopt, std::nullopt, std::nullopt), identity::RequestStatus::New);
  auto contact = contact_manager_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::awaitingApproval);
}

TEST_F(ContactManagerTest, ConfirmContact) {
  contact_manager_->ReceiveContactRequest(peer1, std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(contact_manager_->ConfirmContact(peer1));
  auto contact = contact_manager_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);
}

TEST_F(ContactManagerTest, ConfirmNonExistentContact) { ASSERT_FALSE(contact_manager_->ConfirmContact(peer1)); }

TEST_F(ContactManagerTest, MarkAsConfirmed) {
  contact_manager_->AddContact(peer1);
  ASSERT_TRUE(contact_manager_->MarkAsConfirmed(peer1));
  auto contact = contact_manager_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);
}

TEST_F(ContactManagerTest, IsConfirmedContact) {
  contact_manager_->AddContact(peer1);
  ASSERT_FALSE(contact_manager_->IsConfirmedContact(peer1));
  contact_manager_->MarkAsConfirmed(peer1);
  ASSERT_TRUE(contact_manager_->IsConfirmedContact(peer1));
}

TEST_F(ContactManagerTest, DeleteContact) {
  contact_manager_->AddContact(peer1);
  ASSERT_TRUE(contact_manager_->GetContact(peer1).has_value());
  contact_manager_->DeleteContact(peer1);
  ASSERT_FALSE(contact_manager_->GetContact(peer1).has_value());
}

TEST_F(ContactManagerTest, Persistence) {
  contact_manager_->AddContact(peer1);
  contact_manager_->ReceiveContactRequest(peer2, std::nullopt, std::nullopt, std::nullopt);
  contact_manager_->MarkAsConfirmed(peer1);

  // Create new manager with the same storage
  auto new_manager = std::make_unique<identity::ContactManager>(storage_);

  auto contact1 = new_manager->GetContact(peer1);
  ASSERT_TRUE(contact1.has_value());
  EXPECT_EQ(contact1->status, identity::ContactStatus::confirmed);

  auto contact2 = new_manager->GetContact(peer2);
  ASSERT_TRUE(contact2.has_value());
  EXPECT_EQ(contact2->status, identity::ContactStatus::awaitingApproval);

  auto all_contacts = new_manager->GetContacts();
  EXPECT_EQ(all_contacts.size(), 2);
}
