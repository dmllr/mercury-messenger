#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::messaging {

class DeliveryService {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::atomic<bool> is_running_{false};

  std::unordered_set<ContactID> pending_connections_{};
  std::unordered_map<ContactID, int> retry_delays_s_{};
  std::unordered_set<ContactID> retry_scheduled_{};
  mutable std::recursive_mutex mutex_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  DispatchQueue dispatch_queue_{"DeliveryService", 1};

  static constexpr int initialRetryDelayS = 10;
  static constexpr int maxRetryDelayS = 120;

 public:
  explicit DeliveryService(services::ServiceManager* sm)
      : storage_(sm->storage),
        contact_manager_(sm->contact_manager),
        transport_(sm->data_transport) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventConnectionAttemptFinished>(
        [this](auto const& e) -> void { ConnectionAttemptFinished(e.peer_id); }
    ));
  }

  ~DeliveryService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
    Stop();
  }

  void Start() {
    if (is_running_.exchange(true)) {
      return;
    }
    dispatch_queue_.Dispatch([this]() -> void { CheckForAllQueuedMessages(); });
  }

  void Stop() {
    if (!is_running_.exchange(false)) {
      return;
    }
    {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      pending_connections_.clear();
      retry_scheduled_.clear();
    }
    dispatch_queue_.RemovePending();
  }

  void ConnectionAttemptFinished(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    pending_connections_.erase(peer_id);

    if (!is_running_) {
      return;
    }

    if (HasPendingWork(peer_id)) {
      bool is_connected = false;
      if (auto p = transport_->GetPeer(peer_id)) {
        is_connected = p->data_channel_is_open.load();
      }

      if (is_connected) {
        retry_delays_s_.erase(peer_id);
        return;
      }

      if (retry_scheduled_.contains(peer_id)) {
        return;
      }

      int& delay = retry_delays_s_[peer_id];
      if (delay == 0) {
        delay = initialRetryDelayS;
      }

      LGI << "Connection to " << peer_id.ToString() << " failed or did not clear queue. Retrying in " << delay << "s.";

      retry_scheduled_.insert(peer_id);
      dispatch_queue_.DispatchAfter(std::chrono::seconds(delay), [this, peer_id]() -> void {
        {
          std::lock_guard<std::recursive_mutex> const lock(mutex_);
          retry_scheduled_.erase(peer_id);
        }
        AttemptDelivery(peer_id);
      });
      delay = std::min(delay * 2, maxRetryDelayS);
    } else {
      retry_delays_s_.erase(peer_id);
    }
  }

  void AttemptDelivery(ContactID const& peer_id) {
    {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      if (pending_connections_.contains(peer_id)) {
        return;
      }

      if (!HasPendingWork(peer_id)) {
        return;
      }

      LGI << "Delivery requested for " << peer_id.ToString() << ". Requesting connection.";
      pending_connections_.insert(peer_id);
    }

    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
  }

 private:
  [[nodiscard]] auto HasPendingWork(ContactID const& peer_id) const -> bool {
    auto const syncs = storage_->GetSyncRequests();
    if (std::ranges::any_of(syncs, [&peer_id](auto const& req) { return req.target_peer_id == peer_id; })) {
      return true;
    }

    auto const attachments = storage_->GetPendingAttachments(peer_id);
    return std::ranges::any_of(attachments, [](auto const& att) {
      return att.status == AttachmentStatus::pending || att.status == AttachmentStatus::transferring;
    });
  }

  void CheckForAllQueuedMessages() {
    if (!contact_manager_) {
      return;
    }
    auto const all_contacts = contact_manager_->GetContacts();
    for (auto const& contact : all_contacts) {
      if (contact.status != identity::ContactStatus::rejected) {
        AttemptDelivery(contact.peer_id);
      }
    }
  }
};

}  // namespace eillance::messaging
