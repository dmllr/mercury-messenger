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
  std::unordered_map<ContactID, std::vector<std::byte>> pending_tokens_;
  std::mutex tokens_mutex_;

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

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      auto contact = manager_->GetContact(e.peer_id);
      if (contact) {
        if (contact->status == identity::ContactStatus::pendingApproval ||
            contact->status == identity::ContactStatus::confirmed) {
          auto signature = sm_->account->Sign(e.peer_id.Span());
          nlohmann::json content = {
              {"type", contact->status == identity::ContactStatus::pendingApproval ? "crcp_request" : "crcp_confirm"},
              {"signature", crypto::ToHex(signature)},
              {"profile",
               {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
                {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")}}},
          };

          if (contact->status == identity::ContactStatus::pendingApproval) {
            std::lock_guard<std::mutex> const t_lock(tokens_mutex_);
            if (auto it = pending_tokens_.find(e.peer_id); it != pending_tokens_.end()) {
              content["token"] = crypto::ToHex(it->second);
            }
          }

          if (auto peer = transport_->GetPeer(e.peer_id); peer && peer->data_channel_is_open) {
            std::ignore = transport_->SendJson(e.peer_id, content);
          }
        }
      }
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

    if (token) {
      std::lock_guard<std::mutex> const lock(tokens_mutex_);
      pending_tokens_[peer_id] = *token;
    }

    if (manager_->AddContact(peer_id) || (existing && existing->status == identity::ContactStatus::pendingApproval)) {
      auto signature = sm_->account->Sign(peer_id.Span());
      nlohmann::json content = {
          {"type", "crcp_request"},
          {"signature", crypto::ToHex(signature)},
          {"profile",
           {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
            {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")}}},
      };
      if (token) {
        content["token"] = crypto::ToHex(*token);
      }

      if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        std::ignore = transport_->SendJson(peer_id, content);
      }

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
    {
      std::lock_guard<std::mutex> const lock(tokens_mutex_);
      pending_tokens_.erase(peer_id);
    }
    auto contact = manager_->GetContact(peer_id);
    if (!contact) {
      return;
    }

    if (contact->status == identity::ContactStatus::awaitingApproval ||
        contact->status == identity::ContactStatus::pendingApproval) {
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
        {"type", "crcp_confirm"},
        {"signature", crypto::ToHex(signature)},
        {"profile",
         {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
          {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")}}},
    };
    if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      std::ignore = transport_->SendJson(peer_id, content);
    }

    EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
    EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
  }

  void BlockContact(ContactID const& peer_id, bool notify_peer) {
    {
      std::lock_guard<std::mutex> const lock(tokens_mutex_);
      pending_tokens_.erase(peer_id);
    }
    if (notify_peer) {
      nlohmann::json msg = {{"type", "crcp_reject"}};
      if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        std::ignore = transport_->SendJson(peer_id, msg);
      }
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
      auto existing = manager_->GetContact(peer);
      if (existing && existing->status == identity::ContactStatus::confirmed) return;
      std::optional<std::string> token;
      if (json.contains("token")) token = json["token"];
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      OnContactAddRequest(peer, token, profile, std::vector<std::byte>{});
    } else if (type == "contact_confirm") {
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      OnContactConfirm(peer, crypto::FromHex(json.value("signature", "")), profile);
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

  void ProcessCrcpMessage(ContactID const& peer_id, nlohmann::json const& json) {
    std::string const type = json.value("type", "");

    if (type == "crcp_request") {
      std::optional<std::string> token;
      if (json.contains("token")) token = json["token"];
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      std::string signature_hex = json.value("signature", "");

      auto sig = crypto::FromHex(signature_hex);
      if (sig.empty() || !crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
        LGE << "Invalid signature in CRCP request from " << peer_id.ToString();
        return;
      }

      if (auto peer = transport_->GetPeer(peer_id)) peer->crcp_verified = true;

      auto existing = manager_->GetContact(peer_id);
      if (existing && existing->status == identity::ContactStatus::confirmed) {
        manager_->StoreSignatureFrom(peer_id, sig);
        identity::ProfileUpdate p_update;
        if (profile.contains("display_name") && !profile["display_name"].is_null())
          p_update.display_name = profile["display_name"];
        if (profile.contains("bio") && !profile["bio"].is_null()) p_update.bio = profile["bio"];
        if (profile.contains("photo") && !profile["photo"].is_null()) p_update.photo_base64 = profile["photo"];
        manager_->UpdateContactProfile(peer_id, p_update);
        ConfirmContact(peer_id);
        return;
      }
      if (existing && existing->status == identity::ContactStatus::pendingApproval) {
        manager_->StoreSignatureFrom(peer_id, sig);
        ConfirmContact(peer_id);
        return;
      }

      OnContactAddRequest(peer_id, token, profile, sig);
    } else if (type == "crcp_confirm") {
      std::string signature_hex = json.value("signature", "");
      nlohmann::json profile = json.value("profile", nlohmann::json::object());
      auto sig = crypto::FromHex(signature_hex);
      if (sig.empty() || !crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
        LGE << "Invalid signature in CRCP confirm from " << peer_id.ToString();
        return;
      }
      if (auto peer = transport_->GetPeer(peer_id)) peer->crcp_verified = true;
      OnContactConfirm(peer_id, sig, profile);
    } else if (type == "crcp_reject") {
      OnContactRemoved(peer_id);
    }
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
      nlohmann::json const& profile_json,
      std::vector<std::byte> const& signature
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

    if (!signature.empty()) {
      manager_->StoreSignatureFrom(peer_id, signature);
    }

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
  OnContactConfirm(ContactID const& peer_id, std::vector<std::byte> const& sig, nlohmann::json const& profile_json) {
    {
      std::lock_guard<std::mutex> const lock(tokens_mutex_);
      pending_tokens_.erase(peer_id);
    }
    if (!sig.empty()) {
      manager_->StoreSignatureFrom(peer_id, sig);
    }

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
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    }
  }

  void OnContactConfirmAck(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventConnectionRequested{peer_id});
  }

  void OnContactRestored(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
  }

  void OnContactRemoved(ContactID const& peer_id) {
    {
      std::lock_guard<std::mutex> const lock(tokens_mutex_);
      pending_tokens_.erase(peer_id);
    }
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
