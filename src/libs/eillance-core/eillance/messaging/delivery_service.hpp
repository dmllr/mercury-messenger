#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/message_queue.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  using ConnectionRequestHandler = std::function<void(const std::string&)>;

  std::shared_ptr<MessageQueue> message_queue_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  ConnectionRequestHandler connection_handler_;
  DispatchQueue dispatch_queue_{"DeliveryService", 1};
  std::atomic<bool> is_running_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<std::string> pending_connections_;
  std::mutex mutex_;

  static constexpr int INITIAL_RETRY_DELAY_S = 5;
  static constexpr int MAX_RETRY_DELAY_S = 60;

public:
  MessageDeliveryService(std::shared_ptr<MessageQueue> queue,
                         std::shared_ptr<identity::ContactManager> contacts,
                         ConnectionRequestHandler handler)
      : message_queue_(std::move(queue)), contact_manager_(std::move(contacts)), connection_handler_(std::move(handler)) {}

  void start() {
    is_running_ = true;
    scheduleCheck(INITIAL_RETRY_DELAY_S);
  }

  void stop() {
    is_running_ = false;
    dispatch_queue_.RemovePending();
  }

  // Called by RtcClient when a connection attempt is finished (succeeded or failed)
  void connectionAttemptFinished(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    pending_connections_.erase(peer_id);
  }

  // Called by RtcClient when queuing a new message to an offline peer
  void attemptDelivery(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    // Do not initiate connection if there are no messages to send.
    if (message_queue_->GetQueuedMessages(peer_id).empty()) {
      return;
    }

    if (pending_connections_.contains(peer_id)) {
      return;  // Already trying to connect
    }

    LOGI << "Queued messages found for " << peer_id << ". Requesting connection.";
    pending_connections_.insert(peer_id);
    connection_handler_(peer_id);
  }

private:
  void scheduleCheck(int delay_s) {
    if (!is_running_) return;
    dispatch_queue_.DispatchAfter(std::chrono::seconds(delay_s), [this, delay_s]() {
      CheckForQueuedMessages();
      // Exponential backoff
      const auto next_delay = std::min(delay_s * 2, MAX_RETRY_DELAY_S);
      scheduleCheck(next_delay);
    });
  }

  void CheckForQueuedMessages() {
    if (!contact_manager_) {
      return;
    }
    auto all_contacts = contact_manager_->GetContacts();
    for (const auto& [peer_id, status]: all_contacts) {
      if (!message_queue_->GetQueuedMessages(peer_id).empty()) {
        // We have messages for this contact, let's try to deliver them.
        attemptDelivery(peer_id);
      }
    }
  }
};

}  // namespace eillance::messaging
