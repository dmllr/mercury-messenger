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
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

class ContactService {
 public:
  explicit ContactService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport),
        manager_(sm->contact_manager) {
    auto sub7 =
        EventQueue::Instance().Subscribe<EventContactRestored>([this](auto const& e) { OnContactRestored(e.peer_id); });
    event_unsubs_.push_back([sub7]() { EventQueue::Instance().Unsubscribe<EventContactRestored>(sub7); });

    auto sub8 = EventQueue::Instance().Subscribe<EventDebugClearContactCache>([this](auto const&) {
      manager_->ClearContacts();
    });
    event_unsubs_.push_back([sub8]() { EventQueue::Instance().Unsubscribe<EventDebugClearContactCache>(sub8); });

    auto sub9 =
        EventQueue::Instance().Subscribe<EventReloadContactCache>([this](auto const&) { manager_->ReloadContacts(); });
    event_unsubs_.push_back([sub9]() { EventQueue::Instance().Unsubscribe<EventReloadContactCache>(sub9); });
  }

  ~ContactService() {
    for (auto const& unsub : event_unsubs_) unsub();
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

    if (manager_->AddContact(peer_id)) {
      nlohmann::json content = {
          {"action", "contact_request"},
          {"profile",
           {{"display_name", sm_->account->GetSettings()->GetMyDisplayName().value_or("")},
            {"bio", sm_->account->GetSettings()->GetMyBio().value_or("")},
            {"photo", sm_->account->GetSettings()->GetMyPhotoBase64()}}},
      };
      if (token) {
        content["token"] = crypto::ToHex(*token);
      }

      sm_->messaging->SendSystemMessage(peer_id, content.dump(), true);
      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::pendingApproval});
    }
    return true;
  }

  void ConfirmContact(ContactID const& peer_id) {
    if (manager_->ConfirmContact(peer_id)) {
      sm_->messaging->SendSystemMessage(peer_id, nlohmann::json{{"action", "contact_confirm"}}.dump(), true);

      auto signature = sm_->account->Sign(peer_id.Span());
      manager_->StoreMySignatureFor(peer_id, signature);

      sm_->messaging->SendSystemMessage(
          peer_id,
          nlohmann::json{{"action", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump(),
          true
      );

      storage_->AddPendingAction(
          peer_id,
          nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
      );

      // This system message is just for local history.
      sm_->messaging->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump(), true);

      // Send our profile and request theirs
      sm_->profile->SendMyProfile(peer_id);

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

      // Ensure connection and flush queue.
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
      sm_->messaging->SendQueuedMessages(peer_id);
    }
  }

  void BlockContact(ContactID const& peer_id, bool notify_peer) {
    if (notify_peer) {
      storage_->AddPendingAction(peer_id, nlohmann::json{{"type", "contact_removed"}}.dump());
      sm_->messaging->SendQueuedMessages(peer_id);
    }

    transport_->Disconnect(peer_id);
    transport_->RemovePeer(peer_id);
    manager_->BlockContact(peer_id);
  }

  void DeleteContact(ContactID const& peer_id) {
    BlockContact(peer_id, false);
    manager_->DeleteContact(peer_id);
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

  // Debug Actions
  void DebugClearCache() {
    manager_->ClearContacts();
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
    std::optional<std::string> name;
    std::optional<std::string> bio;
    std::optional<std::string> photo;
    if (profile_json.contains("display_name") && !profile_json["display_name"].is_null()) {
      name = profile_json["display_name"];
    }
    if (profile_json.contains("bio") && !profile_json["bio"].is_null()) {
      bio = profile_json["bio"];
    }
    if (profile_json.contains("photo") && !profile_json["photo"].is_null()) {
      photo = profile_json["photo"];
    }

    auto status = manager_->ReceiveContactRequest(peer_id, name, bio, photo);

    if (name || bio || photo) {
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

      nlohmann::json const content = {{"action", "contact_request_received"}};
      sm_->messaging->AddSystemEntry(peer_id, content.dump(), messaging::MessageDirection::received);
      // MarkMessagesAsRead with empty mids is effectively just a history refresh.
      sm_->messaging->MarkMessagesAsRead(peer_id, {});

      if (status == identity::RequestStatus::New) {
        EventQueue::Instance().Push(EventContactRequest{peer_id});
      }
    } else {
      // Reinstall recovery: If contact already confirmed, resend proofs.
      if (manager_->IsConfirmedContact(peer_id)) {
        // Send directly since this is low-level signaling bypass
        // Actually, best to queue it up properly via action
        nlohmann::json const msg = {{"type", "contact_confirm"}, {"to", peer_id.ToString()}};
        storage_->AddPendingAction(peer_id, msg.dump());

        auto const signature = sm_->account->Sign(peer_id.Span());
        storage_->AddPendingAction(
            peer_id,
            nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
        );

        sm_->profile->SendMyProfile(peer_id);

        EventQueue::Instance().Push(EventConnectionRequested{peer_id});
        sm_->messaging->SendQueuedMessages(peer_id);
      }
    }
  }

  void OnContactConfirm(ContactID const& peer_id) {
    if (manager_->MarkAsConfirmed(peer_id)) {
      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

      sm_->messaging->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump(), true);
      sm_->messaging->SendSystemMessage(peer_id, nlohmann::json{{"action", "request_profile"}}.dump(), true);

      sm_->profile->SendMyProfile(peer_id);
      sm_->profile->RequestProfile(peer_id);
    }
  }

  void OnContactRestored(ContactID const& peer_id) {
    sm_->profile->RequestProfile(peer_id);
    EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});
  }

  void OnContactRemoved(ContactID const& peer_id) {
    EventQueue::Instance().Push(EventContactRemoved{peer_id});
    BlockContact(peer_id, false);
  }

  void OnReconfirmRequest(ContactID const& peer_id) {
    if (auto contact = manager_->GetContact(peer_id); contact && contact->my_signature_for_them) {
      nlohmann::json const resp = {
          {"type", "reconfirm_response"},
          {"signature", crypto::ToHex(*contact->my_signature_for_them)}
      };
      storage_->AddPendingAction(peer_id, resp.dump());
      sm_->messaging->SendQueuedMessages(peer_id);
    }
  }

  void OnReconfirmResponse(ContactID const& peer_id, std::string const& signature_hex) {
    auto sig = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
      manager_->MarkAsConfirmed(peer_id);

      auto my_sig = sm_->account->Sign(peer_id.Span());
      manager_->StoreMySignatureFor(peer_id, my_sig);
      manager_->StoreMySignatureFor(peer_id, my_sig);

      storage_->AddPendingAction(
          peer_id,
          nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(my_sig)}}.dump()
      );

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

      sm_->profile->RequestProfile(peer_id);
      sm_->messaging->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump(), true);

      manager_->ReloadContacts();
      sm_->profile->SendMyProfile(peer_id);
    }
  }

  void OnContactSignature(ContactID const& peer_id, std::string const& signature_hex) {
    auto sig = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
      manager_->StoreSignatureFrom(peer_id, sig);
    }
  }

 private:
  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;
  std::vector<std::function<void()>> event_unsubs_;
};

}  // namespace eillance::services
