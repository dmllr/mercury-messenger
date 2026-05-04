#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

class ContactService {
 public:
  explicit ContactService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport),
        manager_(sm->contact_manager) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventContactRestored>([this](auto const& e) {
      OnContactRestored(e.peer_id);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventReloadContactCache>([this](auto const&) {
      manager_->ReloadContacts();
    }));
  }

  ~ContactService() {
    for (auto const id : event_unsubs_) EventQueue::Instance().Unsubscribe(id);
  }

  // Actions
  auto AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) -> bool {
    if (peer_id == sm_->account->GetId()) {
      LGW << "Attempted to add self as contact.";
      return false;
    }

    auto const existing = manager_->GetContact(peer_id);
    if (existing && existing->status == identity::ContactStatus::awaitingApproval) {
      ConfirmContact(peer_id);
      return true;
    }

    if (manager_->AddContact(peer_id) || (existing && existing->status == identity::ContactStatus::pendingApproval)) {
      nlohmann::json content = {
          {"type", "contact_add_request"},
          {"to", peer_id.ToString()},
          {"profile",
           {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
            {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")}}},
      };
      if (token) {
        content["token"] = crypto::ToHex(*token);
      }

      sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::ContactState, content);

      if (!existing || existing->status != identity::ContactStatus::pendingApproval) {
        sm_->messaging->AddSystemEntry(
            peer_id,
            nlohmann::json{{"action", "contact_request"}}.dump(),
            messaging::MessageDirection::sent
        );
      }

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::pendingApproval});
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
    }
    return true;
  }

  void ConfirmContact(ContactID const& peer_id) {
    auto contact = manager_->GetContact(peer_id);
    if (!contact) {
      return;
    }

    if (contact->status == identity::ContactStatus::awaitingApproval) {
      if (!manager_->ConfirmContact(peer_id)) {
        return;
      }
      sm_->messaging->AddSystemEntry(
          peer_id,
          nlohmann::json{{"action", "contact_confirm"}}.dump(),
          messaging::MessageDirection::sent
      );
    } else if (contact->status != identity::ContactStatus::confirmed) {
      return;
    }

    auto signature = sm_->account->Sign(peer_id.Span());
    manager_->StoreMySignatureFor(peer_id, signature);

    nlohmann::json const content = {
        {"type", "contact_confirm"},
        {"to", peer_id.ToString()},
        {"signature", crypto::ToHex(signature)},
        {"profile",
         {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
          {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")}}},
    };
    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::ContactState, content);

    EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
  }

  void BlockContact(ContactID const& peer_id, bool notify_peer) {
    if (notify_peer) {
      nlohmann::json msg = {{"type", "contact_removed"}, {"to", peer_id.ToString()}};
      sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::ContactState, msg);
    }

    sm_->media_transport->Disconnect(peer_id);
    transport_->Disconnect(peer_id);
    transport_->RemovePeer(peer_id);
    manager_->BlockContact(peer_id);
  }

  void DeleteContact(ContactID const& peer_id) {
    BlockContact(peer_id, false);
    manager_->DeleteContact(peer_id);
  }

  void ProjectContactState(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) return;
    std::string const type = json.value("type", "");
    ContactID const peer(entry.topic_id.value.Span());

    if (type == "contact_add_request") {
      std::optional<std::string> token;
      if (json.contains("token")) token = json["token"];
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      OnContactAddRequest(peer, token, profile);
    } else if (type == "contact_confirm") {
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      OnContactConfirm(peer, json.value("signature", ""), profile);
    } else if (type == "contact_removed") {
      OnContactRemoved(peer);
    }
  }

  void UnblockContact(ContactID const& peer_id) {
    if (manager_->UnblockContact(peer_id)) {
      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::awaitingApproval});
    }
  }

  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
    manager_->SetDisplayNameOverride(peer_id, name);
  }

  void ResetContactDisplayNameOverride(ContactID const& peer_id) {
    manager_->ResetDisplayNameOverride(peer_id);
    sm_->profile->RequestProfile(peer_id);
  }

  auto GenerateContactToken() -> std::vector<std::byte> {
    auto arr = manager_->GenerateContactToken();
    return {arr.begin(), arr.end()};
  }
  void ReloadCache() {
    manager_->ReloadContacts();
  }

  // Queries
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact> {
    return manager_->GetContacts();
  }
  [[nodiscard]] auto GetContact(ContactID const& peer_id) const -> std::optional<identity::Contact> {
    return manager_->GetContact(peer_id);
  }
  [[nodiscard]] auto IsConfirmed(ContactID const& peer_id) const -> bool {
    return manager_->IsConfirmedContact(peer_id);
  }

  // Event Handlers
  void OnContactAddRequest(
      ContactID const& peer_id,
      std::optional<std::string> const& token,
      nlohmann::json const& profile_json
  ) {
    identity::ProfileUpdate profile;
    if (profile_json.contains("display_name") && !profile_json["display_name"].is_null()) {
      profile.display_name = profile_json["display_name"];
    }
    if (profile_json.contains("bio") && !profile_json["bio"].is_null()) {
      profile.bio = profile_json["bio"];
    }
    if (profile_json.contains("photo") && !profile_json["photo"].is_null()) {
      profile.photo_base64 = profile_json["photo"];
    }

    auto status = manager_->ReceiveContactRequest(peer_id, profile);

    if (profile.display_name || profile.bio || profile.photo_base64) {
      EventQueue::Instance().Push(EventContactProfileUpdated{peer_id});
    }

    if (status == identity::RequestStatus::New || status == identity::RequestStatus::duplicate) {
      bool valid_token = false;
      if (token) {
        if (manager_->ValidateAndConsumeContactToken(crypto::FromHex(*token))) {
          valid_token = true;
        }
      }

      if (valid_token) {
        ConfirmContact(peer_id);
        return;
      }

      if (status == identity::RequestStatus::New) {
        nlohmann::json const content = {{"action", "contact_request_received"}};
        sm_->messaging->AddSystemEntry(peer_id, content.dump(), messaging::MessageDirection::received);
        sm_->messaging->MarkMessagesAsRead(peer_id, {});
        EventQueue::Instance().Push(EventContactRequest{peer_id});

        std::string name = profile.display_name ? *profile.display_name : peer_id.ToString().substr(0, 8);
        EventQueue::Instance().Push(EventNotifyContactRequest{peer_id, name});
      }
    } else {
      if (manager_->IsConfirmedContact(peer_id)) {
        ConfirmContact(peer_id);
      }
    }
  }

  void OnContactAddAck(ContactID const& peer_id) {
  }

  void
  OnContactConfirm(ContactID const& peer_id, std::string const& signature_hex, nlohmann::json const& profile_json) {
    auto sig = crypto::FromHex(signature_hex);
    if (!crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
      LGE << "Invalid signature in contact_confirm from " << peer_id.ToString();
      return;
    }

    manager_->StoreSignatureFrom(peer_id, sig);

    bool was_confirmed = manager_->IsConfirmedContact(peer_id);
    if (manager_->MarkAsConfirmed(peer_id)) {
      identity::ProfileUpdate profile;
      if (profile_json.contains("display_name") && !profile_json["display_name"].is_null()) {
        profile.display_name = profile_json["display_name"];
      }
      if (profile_json.contains("bio") && !profile_json["bio"].is_null()) {
        profile.bio = profile_json["bio"];
      }
      if (profile_json.contains("photo") && !profile_json["photo"].is_null()) {
        profile.photo_base64 = profile_json["photo"];
      }
      manager_->UpdateContactProfile(peer_id, profile);

      if (!was_confirmed) {
        sm_->messaging->AddSystemEntry(
            peer_id,
            nlohmann::json{{"action", "contact_confirm"}}.dump(),
            messaging::MessageDirection::received
        );
        sm_->messaging->MarkMessagesAsRead(peer_id, {});
      }

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
    }
  }

  void OnContactConfirmAck(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
  }

  void OnContactRestored(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
  }

  void OnContactRemoved(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventContactRemoved{peer_id});
    BlockContact(peer_id, false);
  }

  void OnContactRemovedAck(ContactID const& peer_id) {
  }

 private:
  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
};

}  // namespace eillance::services
