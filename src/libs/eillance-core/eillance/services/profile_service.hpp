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
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class ProfileService {
 public:
  explicit ProfileService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport),
        manager_(sm->contact_manager) {
  }

  ~ProfileService() {
    for (auto const& unsub : event_unsubs_) unsub();
  }

  // --- Actions ---

  void SendMyProfile(ContactID const& peer_id) {
    nlohmann::json msg;
    msg["type"] = "profile_info";
    msg["display_name"] = sm_->account->GetSettings()->GetMyDisplayName().value_or("");
    msg["bio"] = sm_->account->GetSettings()->GetMyBio().value_or("");

    auto photo = sm_->account->GetSettings()->GetMyPhotoBase64();
    if (photo && !photo->empty()) {
      msg["photo"] = *photo;
    } else {
      msg["photo"] = nullptr;
    }

    storage_->AddPendingAction(peer_id, msg.dump());
    sm_->messaging->SendQueuedMessages(peer_id);
  }

  void RequestProfile(ContactID const& peer_id) {
    storage_->AddPendingAction(peer_id, nlohmann::json{{"type", "profile_request"}}.dump());
    sm_->messaging->SendQueuedMessages(peer_id);
  }

  void SyncProfile(ContactID const& peer_id) {
    sm_->messaging->SendQueuedMessages(peer_id);
  }

  void MarkAllContactsProfileSyncNeeded() {
    auto const contacts = manager_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed ||
          contact.status == identity::ContactStatus::pendingApproval) {
        SendMyProfile(contact.peer_id);
      }
    }
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
      nlohmann::json diff;
      diff["action"] = "profile_update_diff";
      diff["new"] = {{"display_name", name}, {"bio", bio}, {"photo", photo}};
      if (old) {
        diff["old"] = {{"display_name", old->display_name}, {"bio", old->bio}, {"photo", old->photo_base64}};
      }
      sm_->messaging->AddSystemEntry(peer_id, diff.dump(), messaging::MessageDirection::received);
    }

    manager_->UpdateContactProfile(peer_id, name, bio, photo);
    EventQueue::Instance().Push(EventContactProfileUpdated{peer_id});
  }

 private:
  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;
  std::vector<std::function<void()>> event_unsubs_;
};

}  // namespace eillance::services
