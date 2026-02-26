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
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class MessageDeliveryService {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contactManager_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::atomic<bool> isRunning_{false};

  // Track peers for which we are actively trying to connect to avoid duplicate requests.
  std::unordered_set<ContactID> pendingConnections_{};
  std::unordered_map<ContactID, int> retryDelaysS_{};
  std::recursive_mutex mutex_;

  DispatchQueue dispatchQueue_{"DeliveryService", 1};

  static constexpr int initialRetryDelayS = 5;
  static constexpr int maxRetryDelayS = 60;
  static constexpr uint64_t kSuspensionThresholdUs = 48 * 3600 * 1000000ULL;  // 2 days
  static constexpr std::chrono::hours kSuspensionCheckInterval{1};

 public:
  MessageDeliveryService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<identity::ContactManager> contacts,
      std::shared_ptr<transport::DataTransport> transport
  )
      : storage_(std::move(storage)),
        contactManager_(std::move(contacts)),
        transport_(std::move(transport)) {
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
    if (!storage_->GetPendingActions(peer_id).empty()) {
      has_work = true;
    } else {
      auto const attachments = storage_->GetPendingAttachments(peer_id);
      has_work = std::ranges::any_of(attachments, [](auto const& att) {
        return att.status == AttachmentStatus::pending || att.status == AttachmentStatus::transferring;
      });
    }

    // Check if there are more messages to send.
    if (has_work) {
      bool is_connected = false;
      if (auto p = transport_->GetPeer(peer_id)) {
        is_connected = p->data_channel_is_open.load();
      }

      if (is_connected) {
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
      if (!storage_->GetPendingActions(peer_id).empty()) {
        has_work = true;
      } else {
        auto const attachments = storage_->GetPendingAttachments(peer_id);
        has_work = std::ranges::any_of(attachments, [](auto const& att) {
          return att.status == AttachmentStatus::pending || att.status == AttachmentStatus::transferring;
        });
      }

      // Prevent connection attempts when there's nothing to do.
      if (!has_work) {
        return;
      }

      LGI << "Delivery requested for " << peer_id << ". Requesting connection.";
      pendingConnections_.insert(peer_id);
    }

    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
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
