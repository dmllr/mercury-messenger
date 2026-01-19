#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  using ConnectionRequestHandler = std::function<void(const ContactID&)>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  ConnectionRequestHandler connection_handler_;
  DispatchQueue dispatch_queue_{"DeliveryService", 1};
  std::atomic<bool> is_running_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<ContactID> pending_connections_;
  std::unordered_map<ContactID, int> retry_delays_s_;
  std::recursive_mutex mutex_;

  static constexpr int initialRetryDelayS = 5;
  static constexpr int maxRetryDelayS = 60;

public:
  MessageDeliveryService(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<identity::ContactManager> contacts, ConnectionRequestHandler handler) : storage_(std::move(storage)), contact_manager_(std::move(contacts)), connection_handler_(std::move(handler)) {}

  void Start() {
    if (is_running_.exchange(true)) {
      return;  // Already running.
    }
    // On start, check for any messages that were queued while offline.
    dispatch_queue_.Dispatch([this] { CheckForAllQueuedMessages(); });
  }

  void Stop() {
    if (!is_running_.exchange(false)) {
      return;  // Already stopped.
    }
    dispatch_queue_.RemovePending();
  }

  // Called by Client when a connection attempt is finished (succeeded and messages sent, or failed)
  void ConnectionAttemptFinished(const ContactID& peer_id) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    pending_connections_.erase(peer_id);

    if (!is_running_) {
      return;
    }

    // Check if there are more messages to send. If so, it means the connection
    // likely failed, and we should schedule a retry.
    if (!storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) {
      int& delay = retry_delays_s_[peer_id];
      if (delay == 0) { delay = initialRetryDelayS; }

      LOGI << "Connection to " << peer_id << " failed or did not clear queue. Retrying in " << delay << "s.";

      dispatch_queue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() { AttemptDelivery(peer_id); });

      // Increase delay for next time (exponential backoff).
      delay = std::min(delay * 2, maxRetryDelayS);
    } else {
      // All messages for this peer were sent successfully. Reset backoff timer.
      retry_delays_s_.erase(peer_id);
    }
  }

  // Called by Client when queuing a new message or action that requires a connection.
  void AttemptDelivery(const ContactID& peer_id) {
    {
      const std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (pending_connections_.contains(peer_id)) {
        // Already trying to connect.
        return;
      }

      // Prevent connection attempts when there's nothing to do.
      if (storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) {
        return;
      }

      LOGI << "Delivery requested for " << peer_id << ". Requesting connection.";
      pending_connections_.insert(peer_id);
    }
    // Perform the callback outside the lock to prevent deadlocks
    connection_handler_(peer_id);
  }

private:
  void CheckForAllQueuedMessages() {
    if (!contact_manager_) { return; }
    const auto all_contacts = contact_manager_->GetContacts();
    for (const auto& contact: all_contacts) {
      const ContactID id(contact.peer_id);
      // Check if there are any messages marked as 'sending' for this contact.
      if (!storage_->GetMessagesWithStatus(id, MessageStatus::sending).empty()) {
        // We have messages for this contact, let's try to deliver them.
        AttemptDelivery(id);
      }
    }
  }
};

}  // namespace eillance::messaging
