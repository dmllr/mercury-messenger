#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "../../src/libs/eillance-core/eillance/services/delivery_service.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class DeliveryServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    conn_sub_id_ = EventQueue::Instance().Subscribe<EventConnectionRequested>([this](auto const& e) {
      std::lock_guard<std::mutex> const lock(mtx_);
      requested_connections_.push_back(e.peer_id);
      cv_.notify_one();
    });

    sm_ = std::make_unique<services::ServiceManager>();
    sm_->storage = storage_;
    sm_->contact_manager = contact_manager_;
    sm_->data_transport = transport_;
    delivery_service_ = std::make_unique<messaging::DeliveryService>(sm_.get());
  }

  void TearDown() override {
    EventQueue::Instance().Unsubscribe(conn_sub_id_);
    delivery_service_->Stop();
  }

  // Helper to wait for a certain number of connection requests with a timeout
  auto WaitForRequests(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) -> bool {
    std::unique_lock<std::mutex> lock(mtx_);
    return cv_.wait_for(lock, timeout, [this, count] { return requested_connections_.size() >= count; });
  }

  void AddPendingMessage(ContactID const& peer_id) {
    auto entry = mock_storage_impl_->AddHistoryEntry(  //
        {.kind = messaging::HistoryEntryKind::textMessage,
         .timestamp = Time::Now(),
         .direction = messaging::MessageDirection::sent,
         .status = messaging::MessageStatus::sending,
         .data = messaging::MessageEntry{"test payload"},
         .peer_id = peer_id}
    );

    // Also add Pending Action to satisfy HasPendingWork logic
    nlohmann::json const action = {{"type", "chat"}, {"message_id", entry.id}};
    mock_storage_impl_->AddPendingAction(
        {.peer_id = peer_id, .payload = action.dump(), .timestamp = Time::Now(), .ref_id = entry.id}
    );
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contact_manager_;

  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  std::shared_ptr<transport::DataTransport> transport_;

  std::unique_ptr<services::ServiceManager> sm_;
  std::unique_ptr<messaging::DeliveryService> delivery_service_;
  uint64_t conn_sub_id_ = 0;

  std::vector<ContactID> requested_connections_;
  std::mutex mtx_;
  std::condition_variable cv_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(DeliveryServiceTest, NoActionWhenNoQueuedMessages) {
  contact_manager_->AddContact(peer1);
  delivery_service_->Start();
  EXPECT_FALSE(WaitForRequests(1, std::chrono::milliseconds(500)));
  EXPECT_TRUE(requested_connections_.empty());
}

TEST_F(DeliveryServiceTest, AttemptDeliveryCallsHandler) {
  AddPendingMessage(peer1);
  delivery_service_->AttemptDelivery(peer1);
  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_.size(), 1);
  EXPECT_EQ(requested_connections_[0], peer1);
}

TEST_F(DeliveryServiceTest, AttemptDeliveryIsDebounced) {
  AddPendingMessage(peer1);
  delivery_service_->AttemptDelivery(peer1);
  delivery_service_->AttemptDelivery(peer1);  // This one should be ignored
  ASSERT_TRUE(WaitForRequests(1, std::chrono::milliseconds(100)));
  EXPECT_FALSE(WaitForRequests(2, std::chrono::milliseconds(100)));
  EXPECT_EQ(requested_connections_.size(), 1);
}

TEST_F(DeliveryServiceTest, CanAttemptDeliveryAgainAfterFinished) {
  AddPendingMessage(peer1);
  delivery_service_->AttemptDelivery(peer1);
  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_.size(), 1);

  delivery_service_->ConnectionAttemptFinished(peer1);

  delivery_service_->AttemptDelivery(peer1);
  ASSERT_TRUE(WaitForRequests(2));
  EXPECT_EQ(requested_connections_.size(), 2);
}

TEST_F(DeliveryServiceTest, StartTriggersCheckForQueuedMessages) {
  // Setup state before starting the service
  contact_manager_->AddContact(peer1);
  AddPendingMessage(peer1);

  delivery_service_->Start();

  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_[0], peer1);
}

TEST_F(DeliveryServiceTest, StopPreventsFurtherChecks) {
  delivery_service_->Start();
  delivery_service_->Stop();  // Stop it immediately

  // Now add a message that would have been picked up
  contact_manager_->AddContact(peer1);
  AddPendingMessage(peer1);

  // Wait and check that nothing happened
  EXPECT_FALSE(WaitForRequests(1, std::chrono::milliseconds(100)));
  EXPECT_TRUE(requested_connections_.empty());
}
