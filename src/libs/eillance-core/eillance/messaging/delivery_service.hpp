#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contactManager_;
  ConnectionRequestHandler connectionHandler_;
  DispatchQueue dispatchQueue_{"DeliveryService", 1};
  std::atomic<bool> isRunning_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<ContactID> pendingConnections_{};
  std::unordered_map<ContactID, int> retryDelaysS_{};
  std::recursive_mutex mutex_;

  static constexpr int kInitialRetryDelayS = 5;
  static constexpr int kMaxRetryDelayS = 60;

 public:
  MessageDeliveryService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<identity::ContactManager> contacts,
      ConnectionRequestHandler handler
  )
      : storage_(std::move(storage)),
        contactManager_(std::move(contacts)),
        connectionHandler_(std::move(handler)) {}

  void Start() {
    if (isRunning_.exchange(true)) {
      return;  // Already running.
    }
    // On start, check for any messages that were queued while offline.
    dispatchQueue_.Dispatch([this] { CheckForAllQueuedMessages(); });
  }

  void Stop() {
    if (!isRunning_.exchange(false)) {
      return;  // Already stopped.
    }
    dispatchQueue_.RemovePending();
  }

  // Called by Client when a connection attempt is finished (succeeded and messages sent, or failed)
  void ConnectionAttemptFinished(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    pendingConnections_.erase(peer_id);

    if (!isRunning_) {
      return;
    }

    // Check if there are more messages to send. If so, it means the connection
    // likely failed, and we should schedule a retry.
    if (!storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) {
      int& delay = retryDelaysS_[peer_id];
      if (delay == 0) {
        delay = kInitialRetryDelayS;
      }

      LGI << "Connection to " << peer_id << " failed or did not clear queue. Retrying in " << delay << "s.";

      dispatchQueue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() { AttemptDelivery(peer_id); });

      // Increase delay for next time (exponential backoff).
      delay = std::min(delay * 2, kMaxRetryDelayS);
    } else {
      // All messages for this peer were sent successfully. Reset backoff timer.
      retryDelaysS_.erase(peer_id);
    }
  }

  // Called by Client when queuing a new message or action that requires a connection.
  void AttemptDelivery(ContactID const& peer_id) {
    {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      if (pendingConnections_.contains(peer_id)) {
        // Already trying to connect.
        return;
      }

      // Prevent connection attempts when there's nothing to do.
      if (storage_->GetMessagesWithStatus(peer_id, MessageStatus::sending).empty()) {
        return;
      }

      LGI << "Delivery requested for " << peer_id << ". Requesting connection.";
      pendingConnections_.insert(peer_id);
    }
    // Perform the callback outside the lock to prevent deadlocks
    connectionHandler_(peer_id);
  }

 private:
  void CheckForAllQueuedMessages() {
    if (!contactManager_) {
      return;
    }
    auto const all_contacts = contactManager_->GetContacts();
    for (auto const& contact : all_contacts) {
      // Check if there are any messages marked as 'sending' for this contact.
      if (!storage_->GetMessagesWithStatus(contact.peer_id, MessageStatus::sending).empty()) {
        AttemptDelivery(contact.peer_id);
      }
    }
  }
};

}  // namespace eillance::messaging
