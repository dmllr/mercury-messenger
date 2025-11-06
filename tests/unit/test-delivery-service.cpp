#include "eillance/identity/contact_manager.hpp"
#include "eillance/messaging/delivery_service.hpp"
#include "mock_persistent_storage.hpp"

#include <chrono>
#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace eillance;

class DeliveryServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);

    connection_handler_ = [this](const std::string& peer_id) {
      std::lock_guard<std::mutex> lock(mtx_);
      requested_connections_.push_back(peer_id);
      cv_.notify_one();
    };

    delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(storage_, contact_manager_, connection_handler_);
  }

  void TearDown() override {
    delivery_service_->stop();
  }

  // Helper to wait for a certain number of connection requests with a timeout
  bool WaitForRequests(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    std::unique_lock<std::mutex> lock(mtx_);
    return cv_.wait_for(lock, timeout, [this, count] {
      return requested_connections_.size() >= count;
    });
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::function<void(const std::string&)> connection_handler_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;

  std::vector<std::string> requested_connections_;
  std::mutex mtx_;
  std::condition_variable cv_;

  const std::string peer1_ = "peer1";
};

TEST_F(DeliveryServiceTest, NoActionWhenNoQueuedMessages) {
  contact_manager_->AddContact(peer1_);
  delivery_service_->start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Wait a bit to ensure checks could have run
  EXPECT_TRUE(requested_connections_.empty());
}

TEST_F(DeliveryServiceTest, AttemptDeliveryCallsHandler) {
  delivery_service_->attemptDelivery(peer1_);
  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_.size(), 1);
  EXPECT_EQ(requested_connections_[0], peer1_);
}

TEST_F(DeliveryServiceTest, AttemptDeliveryIsDebounced) {
  delivery_service_->attemptDelivery(peer1_);
  delivery_service_->attemptDelivery(peer1_);  // This one should be ignored
  ASSERT_TRUE(WaitForRequests(1, std::chrono::milliseconds(100)));
  // Wait a bit more to ensure no second call comes through
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(requested_connections_.size(), 1);
}

TEST_F(DeliveryServiceTest, CanAttemptDeliveryAgainAfterFinished) {
  delivery_service_->attemptDelivery(peer1_);
  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_.size(), 1);

  delivery_service_->connectionAttemptFinished(peer1_);

  delivery_service_->attemptDelivery(peer1_);
  ASSERT_TRUE(WaitForRequests(2));
  EXPECT_EQ(requested_connections_.size(), 2);
}

TEST_F(DeliveryServiceTest, StartTriggersCheckForQueuedMessages) {
  // Setup state before starting the service
  contact_manager_->AddContact(peer1_);
  mock_storage_impl_->AddHistoryEntry({.kind = messaging::HistoryEntryKind::textMessage,
                                       .direction = messaging::MessageDirection::sent,
                                       .status = messaging::MessageStatus::sending,
                                       .data = messaging::MessageEntry{"hello"},
                                       .peer_id_for_db = peer1_});

  delivery_service_->start();

  ASSERT_TRUE(WaitForRequests(1));
  EXPECT_EQ(requested_connections_[0], peer1_);
}

TEST_F(DeliveryServiceTest, StopPreventsFurtherChecks) {
  delivery_service_->start();
  delivery_service_->stop();  // Stop it immediately

  // Now add a message that would have been picked up
  contact_manager_->AddContact(peer1_);
  mock_storage_impl_->AddHistoryEntry({.kind = messaging::HistoryEntryKind::textMessage,
                                       .direction = messaging::MessageDirection::sent,
                                       .status = messaging::MessageStatus::sending,
                                       .data = messaging::MessageEntry{"hello"},
                                       .peer_id_for_db = peer1_});

  // Wait and check that nothing happened
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(requested_connections_.empty());
}
