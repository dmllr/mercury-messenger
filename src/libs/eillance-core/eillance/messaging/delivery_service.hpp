#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  using ConnectionRequestHandler = std::function<void(const std::string&)>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  ConnectionRequestHandler connection_handler_;
  DispatchQueue dispatch_queue_{"DeliveryService", 1};
  std::atomic<bool> is_running_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<std::string> pending_connections_;
  std::unordered_map<std::string, int> retry_delays_s_;
  std::recursive_mutex mutex_;

  static constexpr int initial_retry_delay_s = 5;
  static constexpr int max_retry_delay_s = 60;

public:
  MessageDeliveryService(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<identity::ContactManager> contacts, ConnectionRequestHandler handler) : storage_(std::move(storage)), contact_manager_(std::move(contacts)), connection_handler_(std::move(handler)) {}

  void Start() {
    if (is_running_.exchange(true)) {
      return;  // Already running.
    }
    // On start, check for any messages that were queued while offline.
    dispatch_queue_.Dispatch([this] { CheckForAllQueuedMessages(); });
  }

  void stop() {
    if (!is_running_.exchange(false)) {
      return;  // Already stopped.
    }
    dispatch_queue_.RemovePending();
  }

  // Called by Client when a connection attempt is finished (succeeded and messages sent, or failed)
  void connectionAttemptFinished(const std::string& peer_id) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    pending_connections_.erase(peer_id);

    if (!is_running_) { return; }

    // Check if there are more messages to send. If so, it means the connection
    // likely failed, and we should schedule a retry.
    if (!storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) {
      int& delay = retry_delays_s_[peer_id];
      if (delay == 0) { delay = initial_retry_delay_s; }

      LOGI << "Connection to " << peer_id << " failed or did not clear queue. Retrying in " << delay << "s.";

      dispatch_queue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() { attemptDelivery(peer_id); });

      // Increase delay for next time (exponential backoff).
      delay = std::min(delay * 2, max_retry_delay_s);
    } else {
      // All messages for this peer were sent successfully. Reset backoff timer.
      retry_delays_s_.erase(peer_id);
    }
  }

  // Called by Client when queuing a new message or action that requires a connection.
  void attemptDelivery(const std::string& peer_id) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (pending_connections_.contains(peer_id)) { return; }  // Already trying to connect.

    // This check is important to prevent connection attempts when there's nothing to do.
    if (storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) { return; }

    LOGI << "Delivery requested for " << peer_id << ". Requesting connection.";
    pending_connections_.insert(peer_id);
    connection_handler_(peer_id);
  }

private:
  void CheckForAllQueuedMessages() {
    if (!contact_manager_) { return; }
    const auto all_contacts = contact_manager_->GetContacts();
    for (const auto& contact: all_contacts) {
      // Check if there are any messages marked as 'sending' for this contact.
      if (!storage_->GetMessagesWithStatus(contact.peer_id, MessageStatus::sending).empty()) {
        // We have messages for this contact, let's try to deliver them.
        attemptDelivery(contact.peer_id);
      }
    }
  }
};

}  // namespace eillance::messaging
