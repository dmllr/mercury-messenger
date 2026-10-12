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
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class DeliveryService {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contactManager_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::atomic<bool> isRunning_{false};

  std::unordered_set<ContactID> pendingConnections_{};
  std::unordered_map<ContactID, int> retryDelaysS_{};
  std::unordered_set<ContactID> retryScheduled_{};
  std::recursive_mutex mutex_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  DispatchQueue dispatchQueue_{"DeliveryService", 1};

  static constexpr int initialRetryDelayS = 5;
  static constexpr int maxRetryDelayS = 60;
  static constexpr uint64_t kSuspensionThresholdUs = 48 * 3600 * 1000000ULL;  // 2 days
  static constexpr std::chrono::hours kSuspensionCheckInterval{1};

 public:
  explicit DeliveryService(services::ServiceManager* sm)
      : storage_(sm->storage),
        contactManager_(sm->contact_manager),
        transport_(sm->data_transport) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventConnectionAttemptFinished>(
        [this](auto const& e) -> void { ConnectionAttemptFinished(e.peer_id); }
    ));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventStopDeliveryService>([this](auto const&) -> void {
      Stop();
    }));
  }

  ~DeliveryService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
    Stop();
  }

  void Start() {
    if (isRunning_.exchange(true)) {
      return;
    }
    dispatchQueue_.Dispatch([this]() -> void {
      CheckForSuspensions();
      CheckForAllQueuedMessages();
    });
    ScheduleSuspensionCheck();
  }

  void Stop() {
    if (!isRunning_.exchange(false)) {
      return;
    }
    {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      pendingConnections_.clear();
      retryScheduled_.clear();
    }
    dispatchQueue_.RemovePending();
  }

  void ConnectionAttemptFinished(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (!pendingConnections_.contains(peer_id)) {
      return;
    }

    pendingConnections_.erase(peer_id);

    if (!isRunning_) {
      return;
    }

    bool has_work = false;
    if (!storage_->GetPendingActions(peer_id).empty()) {
      has_work = true;
    } else {
      auto const attachments = storage_->GetPendingAttachments(peer_id);
      has_work = std::ranges::any_of(attachments, [](auto const& att) -> bool {
        return att.status == AttachmentStatus::pending || att.status == AttachmentStatus::transferring;
      });
    }

    if (has_work) {
      bool is_connected = false;
      if (auto p = transport_->GetPeer(peer_id)) {
        is_connected = p->data_channel_is_open.load();
      }

      if (is_connected) {
        retryDelaysS_.erase(peer_id);
        return;
      }

      if (retryScheduled_.contains(peer_id)) {
        return;
      }

      int& delay = retryDelaysS_[peer_id];
      if (delay == 0) {
        delay = initialRetryDelayS;
      }

      LGI << "Connection to " << peer_id << " failed or did not clear queue. Retrying in " << delay << "s.";

      retryScheduled_.insert(peer_id);
      dispatchQueue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() -> void {
        {
          std::lock_guard<std::recursive_mutex> const lock(mutex_);
          retryScheduled_.erase(peer_id);
        }
        AttemptDelivery(peer_id);
      });
      delay = std::min(delay * 2, maxRetryDelayS);
    } else {
      retryDelaysS_.erase(peer_id);
    }
  }

  void AttemptDelivery(ContactID const& peer_id) {
    {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      if (pendingConnections_.contains(peer_id)) {
        return;
      }

      bool has_work = false;
      if (!storage_->GetPendingActions(peer_id).empty()) {
        has_work = true;
      } else {
        auto const attachments = storage_->GetPendingAttachments(peer_id);
        has_work = std::ranges::any_of(attachments, [](auto const& att) -> bool {
          return att.status == AttachmentStatus::pending || att.status == AttachmentStatus::transferring;
        });
      }

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
    dispatchQueue_.DispatchAfter(kSuspensionCheckInterval, [this]() -> void {
      CheckForSuspensions();
      ScheduleSuspensionCheck();
    });
  }
};

}  // namespace eillance::messaging
