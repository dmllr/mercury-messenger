#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  using ConnectionRequestHandler = std::function<void(ContactID const&)>;
  using HasWorkCallback = std::function<bool(ContactID const&)>;
  using IsConnectedCallback = std::function<bool(ContactID const&)>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contactManager_;
  ConnectionRequestHandler connectionHandler_;
  HasWorkCallback hasWorkCallback_;
  IsConnectedCallback isConnectedCallback_;
  DispatchQueue dispatchQueue_{"DeliveryService", 1};
  std::atomic<bool> isRunning_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<ContactID> pendingConnections_{};
  std::unordered_map<ContactID, int> retryDelaysS_{};
  std::recursive_mutex mutex_;

  static constexpr int initialRetryDelayS = 5;
  static constexpr int maxRetryDelayS = 60;
  static constexpr uint64_t kSuspensionThresholdUs = 48 * 3600 * 1000000ULL;  // 2 days
  static constexpr std::chrono::hours kSuspensionCheckInterval{1};

 public:
  MessageDeliveryService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<identity::ContactManager> contacts,
      ConnectionRequestHandler handler,
      HasWorkCallback has_work_callback = nullptr,
      IsConnectedCallback is_connected_callback = nullptr
  )
      : storage_(std::move(storage)),
        contactManager_(std::move(contacts)),
        connectionHandler_(std::move(handler)),
        hasWorkCallback_(std::move(has_work_callback)),
        isConnectedCallback_(std::move(is_connected_callback)) {
  }

  void Start() {
    if (isRunning_.exchange(true)) {
      return;  // Already running.
    }
    // On start, check for any messages that were queued while offline.
    dispatchQueue_.Dispatch([this] {
      CheckForSuspensions();
      CheckForAllQueuedMessages();
    });

    ScheduleSuspensionCheck();
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

    bool has_work = false;
    if (hasWorkCallback_) {
      has_work = hasWorkCallback_(peer_id);
    } else {
      // Fallback: Check pending actions directly
      has_work = !storage_->GetPendingActions(peer_id).empty();
    }

    // Check if there are more messages to send.
    if (has_work) {
      // If we are already connected, we assume the transport is healthy and processing
      // the queue (e.g., waiting for ACKs). We do NOT schedule a retry loop here,
      // as that causes duplicate sends and tight loops.
      if (isConnectedCallback_ && isConnectedCallback_(peer_id)) {
        retryDelaysS_.erase(peer_id);
        return;
      }

      // If not connected, or check not provided, we schedule a retry with backoff.
      int& delay = retryDelaysS_[peer_id];
      if (delay == 0) {
        delay = initialRetryDelayS;
      }

      LGI << "Connection to " << peer_id << " failed or did not clear queue. Retrying in " << delay << "s.";

      dispatchQueue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() { AttemptDelivery(peer_id); });

      // Increase delay for next time (exponential backoff).
      delay = std::min(delay * 2, maxRetryDelayS);
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

      bool has_work = false;
      if (hasWorkCallback_) {
        has_work = hasWorkCallback_(peer_id);
      } else {
        has_work = !storage_->GetPendingActions(peer_id).empty();
      }

      // Prevent connection attempts when there's nothing to do.
      if (!has_work) {
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
      // Skip blocked/rejected contacts.
      if (contact.status != identity::ContactStatus::rejected) {
        AttemptDelivery(contact.peer_id);
      }
    }
  }

  void CheckForSuspensions() {
    if (!isRunning_) {
      return;
    }

    if (auto const now = Time::Now(); now > kSuspensionThresholdUs) {
      uint64_t const threshold = now - kSuspensionThresholdUs;
      storage_->SuspendTimedOutActions(threshold);
    }
  }

  void ScheduleSuspensionCheck() {
    if (!isRunning_) {
      return;
    }
    dispatchQueue_.DispatchAfter(kSuspensionCheckInterval, [this] {
      CheckForSuspensions();
      ScheduleSuspensionCheck();
    });
  }
};

}  // namespace eillance::messaging
