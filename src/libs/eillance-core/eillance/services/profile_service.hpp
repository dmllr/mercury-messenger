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
#include "eillance/services/sync_engine.hpp"
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
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventContactStatusChanged>([this](auto const& e) {
      if (e.status == identity::ContactStatus::confirmed) {
        this->SendMyProfile(e.peer_id);
        this->RequestProfile(e.peer_id);
      }
    }));
  }

  ~ProfileService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
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

    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::ProfileUpdate, msg);
  }

  void RequestProfile(ContactID const& peer_id) {
    nlohmann::json const msg = {{"type", "profile_request"}};
    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::ProfileUpdate, msg);
  }

  void SyncProfile(ContactID const& peer_id) {
    sm_->sync_engine->ProposeSync(TopicID(peer_id), peer_id);
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

  void ProjectProfileUpdate(storage::LedgerEntry const& entry, nlohmann::json const& json, bool const is_mine) {
    if (is_mine) {
      return;
    }

    std::string const type = json.value("type", "");
    ContactID const peer(entry.topic_id.value.Span());

    if (type == "profile_info") {
      OnProfileUpdate(peer, json);
    } else if (type == "profile_request") {
      SendMyProfile(peer);
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

    identity::ProfileUpdate profile_update{get_opt("display_name"), get_opt("bio"), get_opt("photo")};

    auto old = manager_->GetContact(peer_id);
    bool changed = false;
    if (profile_update.display_name && (!old || old->display_name != profile_update.display_name)) {
      changed = true;
    }
    if (profile_update.bio && (!old || old->bio != profile_update.bio)) {
      changed = true;
    }
    if (profile_update.photo_base64 && (!old || old->photo_base64 != profile_update.photo_base64)) {
      changed = true;
    }

    if (changed) {
      nlohmann::json diff;
      diff["action"] = "profile_update_diff";
      diff["new"] = {
          {"display_name", profile_update.display_name},
          {"bio", profile_update.bio},
          {"photo", profile_update.photo_base64}
      };
      if (old) {
        diff["old"] = {{"display_name", old->display_name}, {"bio", old->bio}, {"photo", old->photo_base64}};
      }
      sm_->messaging->AddSystemEntry(peer_id, diff.dump(), messaging::MessageDirection::received);
    }

    manager_->UpdateContactProfile(peer_id, profile_update);
    EventQueue::Instance().Push(EventContactProfileUpdated{peer_id});
  }

 private:
  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
};

}  // namespace eillance::services
