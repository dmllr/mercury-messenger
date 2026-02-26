#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class ProfileService {
 public:
  explicit ProfileService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      std::shared_ptr<AccountService> account_service,
      std::shared_ptr<MessagingService> messaging_service
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        manager_(std::move(contact_manager)),
        account_service_(account_service),
        messaging_service_(messaging_service) {
  }

  // --- Actions ---

  // Sends my profile to the peer. Queues the action.
  void SendMyProfile(ContactID const& peer_id) {
    nlohmann::json msg;
    msg["type"] = "profile_info";
    msg["display_name"] = account_service_->GetSettings()->GetMyDisplayName().value_or("");
    msg["bio"] = account_service_->GetSettings()->GetMyBio().value_or("");

    auto photo = account_service_->GetSettings()->GetMyPhotoBase64();
    if (photo && !photo->empty()) {
      msg["photo"] = *photo;
    } else {
      msg["photo"] = nullptr;
    }

    storage_->AddPendingAction(peer_id, msg.dump());
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Requests the peer to send their profile.
  void RequestProfile(ContactID const& peer_id) {
    storage_->AddPendingAction(peer_id, nlohmann::json{{"type", "profile_request"}}.dump());
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Flushes pending profile actions. In unified system, this just calls the central flush.
  void SyncProfile(ContactID const& peer_id) {
    messaging_service_->SendQueuedMessages(peer_id);
  }

  // Marks all confirmed contacts as needing a profile update (e.g. after I change my name).
  void MarkAllContactsProfileSyncNeeded() {
    auto const contacts = manager_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed ||
          contact.status == identity::ContactStatus::pendingApproval) {
        SendMyProfile(contact.peer_id);
      }
    }
  }

  void NotifyProfileUpdated(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventContactProfileUpdated{peer_id});
  }

  // --- Incoming Events ---

  void OnProfileUpdate(ContactID const& peer_id, nlohmann::json const& profile_json) {
    auto get_opt = [&](char const* key) -> std::optional<std::string> {
      if (!profile_json.contains(key)) {
        return std::nullopt;
      }
      if (profile_json[key].is_null()) {
        return "";
      }
      return profile_json[key].get<std::string>();
    };

    auto name = get_opt("display_name");
    auto bio = get_opt("bio");
    auto photo = get_opt("photo");

    auto old = manager_->GetContact(peer_id);
    bool changed = false;
    if (name && (!old || old->display_name != name)) {
      changed = true;
    }
    if (bio && (!old || old->bio != bio)) {
      changed = true;
    }
    if (photo && (!old || old->photo_base64 != photo)) {
      changed = true;
    }

    if (changed) {
      INVARIANT(messaging_service_);
      nlohmann::json diff;
      diff["action"] = "profile_update_diff";
      diff["new"] = {{"display_name", name}, {"bio", bio}, {"photo", photo}};
      if (old) {
        diff["old"] = {{"display_name", old->display_name}, {"bio", old->bio}, {"photo", old->photo_base64}};
      }
      messaging_service_->AddSystemEntry(peer_id, diff.dump(), messaging::MessageDirection::received);
    }

    manager_->UpdateContactProfile(peer_id, name, bio, photo);
    EventQueue::Instance().Push(EventContactProfileUpdated{peer_id});
  }

 private:
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;

  std::shared_ptr<AccountService> account_service_;
  std::shared_ptr<MessagingService> messaging_service_;
};

}  // namespace eillance::services
