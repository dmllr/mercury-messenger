#include <mutex>

#include "eillance/view_model/contacts_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class ContactsViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    vm_ = std::make_shared<ContactsViewModel>(client_);
  }

  std::shared_ptr<ContactsViewModel> vm_;
};

struct MockContactsObserver {
  std::vector<ViewContact> contacts;
  bool updated = false;
  std::mutex mutex;
  uint64_t sub_id;

  MockContactsObserver() {
    sub_id = EventQueue::Instance().Subscribe<EventContactsStateUpdated>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mutex);
      contacts = e.contacts;
      updated = true;
    });
  }
  ~MockContactsObserver() {
    EventQueue::Instance().Unsubscribe(sub_id);
  }

  auto GetContacts() -> std::vector<ViewContact> {
    std::lock_guard<std::mutex> const lock(mutex);
    return contacts;
  }
};

TEST_F(ContactsViewModelTest, InitialLoad) {
  auto observer = std::make_shared<MockContactsObserver>();

  WaitFor([&] { return observer->updated; });
  // Initially empty
  EXPECT_TRUE(observer->GetContacts().empty());
}

TEST_F(ContactsViewModelTest, AddContact) {
  auto const observer = std::make_shared<MockContactsObserver>();
  WaitFor([&]() -> bool { return observer->updated; });

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
  mock_data_transport_->AddPeer(peer1, std::make_shared<transport::Peer>(peer1));

  auto const observer = std::make_shared<MockContactsObserver>();

  // Wait for add to propagate
  WaitFor([&] { return !observer->GetContacts().empty(); }, std::chrono::milliseconds(2000));

  // Clear flag to wait for *next* update
  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  // Simulate connection state change via MockTransport
  EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer1, transport::PeerConnectionState::connected});

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
