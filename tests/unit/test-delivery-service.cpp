#include <chrono>
#include <memory>

#include <gtest/gtest.h>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/event_queue.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

namespace eillance {

class DeliveryServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto mock_dt = std::make_unique<transport::MockDataTransport>();
    mock_dt_ = mock_dt.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(mock_dt));

    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  transport::MockDataTransport* mock_dt_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(DeliveryServiceTest, CheckReachabilityThreshold) {
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  auto const old_time = Time::Now() - (25ULL * 60 * 60 * Time::uS);
  mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = old_time,
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sending,
       .data = messaging::ChatMessageEntry{messaging::MessageContent{"Old message"}},
       .topic_id = TopicID(peer1),
       .author_id = peer1}
  );

  bool unreachable_emitted = false;
  auto const sub = EventQueue::Instance().Subscribe<EventContactUnreachable>([&](auto const& e) -> void {
    if (e.peer_id == peer1 && e.is_unreachable) {
      unreachable_emitted = true;
    }
  });

  // Trigger regular delivery check which should mark it as unreachable
  EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});

  EventQueue::Instance().WaitFor([&]() -> bool { return unreachable_emitted; }, std::chrono::seconds(1));

  EXPECT_TRUE(unreachable_emitted);
  EXPECT_TRUE(client_->IsUnreachable(TopicID(peer1)));

  // Now test that manual retry clears the unreachable state
  client_->RetrySync(TopicID(peer1));
  EXPECT_FALSE(client_->IsUnreachable(TopicID(peer1)));

  EventQueue::Instance().Unsubscribe(sub);
}

TEST_F(DeliveryServiceTest, ContactUnwillingIgnoresRetry) {
  EventQueue::Instance().Push(EventContactUnwilling{peer1, true});
  EventQueue::Instance().WaitFor([]() -> bool { return false; }, std::chrono::milliseconds(50));

  bool connection_requested = false;
  auto const sub = EventQueue::Instance().Subscribe<EventConnectionRequested>([&](auto const& e) {
    if (e.peer_id == peer1) {
      connection_requested = true;
    }
  });

  client_->RetrySync(TopicID(peer1));
  EventQueue::Instance().WaitFor([]() -> bool { return false; }, std::chrono::milliseconds(100));
  EXPECT_FALSE(connection_requested) << "Unwilling contacts should not trigger connections";
  EventQueue::Instance().Unsubscribe(sub);
}

TEST_F(DeliveryServiceTest, CheckUnreachableForGroupMember) {
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();
  auto gid = client_->CreateGroup("G1", {peer1});

  auto const old_time = Time::Now() - (25ULL * 60 * 60 * Time::uS);
  mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = old_time,
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sending,
       .data = messaging::GroupMessageEntry{messaging::MessageContent{"Old group message"}, {}},
       .topic_id = TopicID(gid),
       .author_id = client_->GetId()}
  );

  bool unreachable_emitted = false;
  auto const sub = EventQueue::Instance().Subscribe<EventContactUnreachable>([&](auto const& e) -> void {
    if (e.peer_id == peer1 && e.is_unreachable) {
      unreachable_emitted = true;
    }
  });

  EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});

  EventQueue::Instance().WaitFor([&]() -> bool { return unreachable_emitted; }, std::chrono::seconds(1));

  EXPECT_TRUE(unreachable_emitted);
  EventQueue::Instance().Unsubscribe(sub);
}

}  // namespace eillance
