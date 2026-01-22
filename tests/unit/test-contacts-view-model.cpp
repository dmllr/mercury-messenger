#include <mutex>

#include "eillance/view_model/contacts_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class ContactsViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<ContactsViewModel>(client_, router_);
  }

  std::shared_ptr<ContactsViewModel> vm_;
};

struct MockContactsObserver : public ContactsViewModel::Observer {
  std::vector<ViewContact> contacts;
  bool updated = false;
  std::mutex mutex;

  void OnContactsUpdated(std::vector<ViewContact> const& c) override {
    std::lock_guard<std::mutex> const lock(mutex);
    contacts = c;
    updated = true;
  }

  auto GetContacts() -> std::vector<ViewContact> {
    std::lock_guard<std::mutex> const lock(mutex);
    return contacts;
  }
};

TEST_F(ContactsViewModelTest, InitialLoad) {
  auto observer = std::make_shared<MockContactsObserver>();
  vm_->SetObserver(observer);

  // Initially empty
  EXPECT_TRUE(observer->GetContacts().empty());
}

TEST_F(ContactsViewModelTest, AddContact) {
  auto observer = std::make_shared<MockContactsObserver>();
  vm_->SetObserver(observer);

  vm_->AddContact(peer1, std::nullopt);

  // Wait specifically for the contact to appear. Increased timeout slightly for thread scheduling.
  WaitFor(
      [&] {
        auto c = observer->GetContacts();
        return !c.empty() && c[0].peer_id == peer1;
      },
      std::chrono::milliseconds(2000)
  );

  auto contacts = observer->GetContacts();
  ASSERT_FALSE(contacts.empty());
  auto const& contact = contacts[0];
  EXPECT_EQ(contact.peer_id, peer1);
  EXPECT_EQ(contact.status, identity::ContactStatus::pendingApproval);
}

TEST_F(ContactsViewModelTest, ContactConnectionState) {
  vm_->AddContact(peer1, std::nullopt);
  // Simulate Transport adding the peer
  mock_transport_->AddPeer(peer1, std::make_shared<transport::Peer>(peer1));

  auto observer = std::make_shared<MockContactsObserver>();
  vm_->SetObserver(observer);

  // Wait for add to propagate
  WaitFor([&] { return !observer->GetContacts().empty(); }, std::chrono::milliseconds(2000));

  // Clear flag to wait for *next* update
  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  // Simulate connection state change via MockTransport
  if (mock_transport_->status_callback) {
    mock_transport_->status_callback(
        peer1,
        EventSource::peerConnectionState,
        static_cast<int>(transport::PeerConnectionState::connected)
    );
  }

  // Wait for update and specific state
  WaitFor(
      [&] {
        auto c = observer->GetContacts();
        return !c.empty() && c[0].connection_state == transport::PeerConnectionState::connected;
      },
      std::chrono::milliseconds(2000)
  );

  auto contacts = observer->GetContacts();
  ASSERT_FALSE(contacts.empty());
  EXPECT_EQ(contacts[0].connection_state, transport::PeerConnectionState::connected);
}

TEST_F(ContactsViewModelTest, BlockAndUnblock) {
  vm_->AddContact(peer1, std::nullopt);
  auto observer = std::make_shared<MockContactsObserver>();
  vm_->SetObserver(observer);

  WaitFor([&] { return !observer->GetContacts().empty(); }, std::chrono::milliseconds(2000));

  // Block
  vm_->BlockContact(peer1);
  WaitFor(
      [&] {
        auto c = observer->GetContacts();
        return !c.empty() && c[0].status == identity::ContactStatus::rejected;
      },
      std::chrono::milliseconds(2000)
  );

  auto contacts = observer->GetContacts();
  ASSERT_FALSE(contacts.empty());
  EXPECT_EQ(contacts[0].status, identity::ContactStatus::rejected);

  // Unblock
  vm_->UnblockContact(peer1);
  WaitFor(
      [&] {
        auto c = observer->GetContacts();
        return !c.empty() && c[0].status == identity::ContactStatus::awaitingApproval;
      },
      std::chrono::milliseconds(2000)
  );

  contacts = observer->GetContacts();
  ASSERT_FALSE(contacts.empty());
  EXPECT_EQ(contacts[0].status, identity::ContactStatus::awaitingApproval);
}
