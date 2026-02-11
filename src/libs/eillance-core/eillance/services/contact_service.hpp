#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class ContactService {
 public:
  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  explicit ContactService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      AccountService* account_service,
      ProfileService* profile_service,
      ConnectionRequestHandler connection_handler
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        manager_(std::move(contact_manager)),
        account_service_(account_service),
        profile_service_(profile_service),
        connection_handler_(std::move(connection_handler)) {
  }

  void SetMessagingService(MessagingService* messaging_service) {
    messaging_service_ = messaging_service;
  }

  // Actions
  auto AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) -> bool {
    if (peer_id == account_service_->GetId()) {
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
           {{"display_name", account_service_->GetSettings()->GetMyDisplayName().value_or("")},
            {"bio", account_service_->GetSettings()->GetMyBio().value_or("")},
            {"photo", account_service_->GetSettings()->GetMyPhotoBase64()}}},
      };
      if (token) {
        content["token"] = crypto::ToHex(*token);
      }

      INVARIANT(messaging_service_);
      messaging_service_->SendSystemMessage(peer_id, content.dump());

      INVARIANT(on_contact_status_changed_);
      on_contact_status_changed_(peer_id, identity::ContactStatus::pendingApproval);
    }
    return true;
  }

  void ConfirmContact(ContactID const& peer_id) {
    if (manager_->ConfirmContact(peer_id)) {
      INVARIANT(messaging_service_);
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "contact_confirm"}}.dump());

      auto signature = account_service_->Sign(peer_id.Span());
      manager_->StoreMySignatureFor(peer_id, signature);

      messaging_service_->SendSystemMessage(
          peer_id,
          nlohmann::json{{"action", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
      );

      storage_->AddPendingAction(
          peer_id,
          nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
      );

      // This system message is just for local history.
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());

      // Send our profile and request theirs
      profile_service_->SendMyProfile(peer_id);  // Queues profile action

      INVARIANT(on_contact_status_changed_);
      on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

      // Ensure connection and flush queue.
      connection_handler_(peer_id);
      messaging_service_->SendQueuedMessages(peer_id);
    }
  }

  void BlockContact(ContactID const& peer_id, bool notify_peer) {
    if (notify_peer) {
      storage_->AddPendingAction(peer_id, nlohmann::json{{"type", "contact_removed"}}.dump());
      if (messaging_service_ != nullptr) {
        messaging_service_->SendQueuedMessages(peer_id);
      }
    }

    transport_->ClosePeerConnection(peer_id, transport::ConnectionContext::Data);
    transport_->ClosePeerConnection(peer_id, transport::ConnectionContext::Media);
    transport_->RemovePeer(peer_id);

    manager_->BlockContact(peer_id);
  }

  void DeleteContact(ContactID const& peer_id) {
    BlockContact(peer_id, false);
    manager_->DeleteContact(peer_id);
  }

  void UnblockContact(ContactID const& peer_id) {
    if (manager_->UnblockContact(peer_id)) {
      INVARIANT(on_contact_status_changed_);
      on_contact_status_changed_(peer_id, identity::ContactStatus::awaitingApproval);
    }
  }

  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
    manager_->SetDisplayNameOverride(peer_id, name);
  }

  void ResetContactDisplayNameOverride(ContactID const& peer_id) {
    manager_->ResetDisplayNameOverride(peer_id);
    profile_service_->RequestProfile(peer_id);
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

      INVARIANT(messaging_service_);
      nlohmann::json const content = {{"action", "contact_request_received"}};
      messaging_service_->AddSystemEntry(peer_id, content.dump(), messaging::MessageDirection::received);
      messaging_service_->MarkMessagesAsRead(peer_id, {});

      if (status == identity::RequestStatus::New) {
        INVARIANT(on_contact_request_);
        on_contact_request_(peer_id);
      }
    } else {
      // Reinstall recovery: If contact already confirmed, resend proofs.
      if (manager_->IsConfirmedContact(peer_id)) {
        transport_->SendSignalingMessage({{"type", "contact_confirm"}, {"to", peer_id.ToString()}});

        auto const signature = account_service_->Sign(peer_id.Span());
        storage_->AddPendingAction(
            peer_id,
            nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
        );

        profile_service_->SendMyProfile(peer_id);

        connection_handler_(peer_id);
        INVARIANT(messaging_service_);
        messaging_service_->SendQueuedMessages(peer_id);
      }
    }
  }

  void OnContactConfirm(ContactID const& peer_id) {
    if (manager_->MarkAsConfirmed(peer_id)) {
      INVARIANT(on_contact_status_changed_);
      on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

      INVARIANT(messaging_service_);
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "request_profile"}}.dump());

      profile_service_->SendMyProfile(peer_id);
      profile_service_->RequestProfile(peer_id);
    }
  }

  void OnContactRestored(ContactID const& peer_id) {
    profile_service_->RequestProfile(peer_id);
    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);
  }

  void OnContactRemoved(ContactID const& peer_id) {
    INVARIANT(on_contact_removed_);
    on_contact_removed_(peer_id);
    BlockContact(peer_id, false);
  }

  void OnReconfirmRequest(ContactID const& peer_id) {
    if (auto contact = manager_->GetContact(peer_id); contact && contact->my_signature_for_them) {
      nlohmann::json const resp = {
          {"type", "reconfirm_response"},
          {"signature", crypto::ToHex(*contact->my_signature_for_them)}
      };
      storage_->AddPendingAction(peer_id, resp.dump());
      INVARIANT(messaging_service_);
      messaging_service_->SendQueuedMessages(peer_id);
    }
  }

  void OnReconfirmResponse(ContactID const& peer_id, std::string const& signature_hex) {
    auto sig = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.Span(), account_service_->GetId().Span(), sig)) {
      manager_->MarkAsConfirmed(peer_id);

      auto my_sig = account_service_->Sign(peer_id.Span());
      manager_->StoreMySignatureFor(peer_id, my_sig);

      storage_->AddPendingAction(
          peer_id,
          nlohmann::json{{"type", "contact_signature"}, {"signature", crypto::ToHex(my_sig)}}.dump()
      );

      INVARIANT(on_contact_status_changed_);
      on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

      profile_service_->RequestProfile(peer_id);
      INVARIANT(messaging_service_);
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());

      manager_->ReloadContacts();
      profile_service_->SendMyProfile(peer_id);
    }
  }

  void OnContactSignature(ContactID const& peer_id, std::string const& signature_hex) {
    auto sig = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.Span(), account_service_->GetId().Span(), sig)) {
      manager_->StoreSignatureFrom(peer_id, sig);
    }
  }

  // Callbacks
  using OnContactRequestCallback = std::function<void(ContactID const& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(ContactID const& peer_id, identity::ContactStatus status)>;
  using OnContactRemovedCallback = std::function<void(ContactID const& peer_id)>;

  void SetContactRequestCallback(OnContactRequestCallback cb) {
    on_contact_request_ = std::move(cb);
  }
  void SetContactStatusChangedCallback(OnContactStatusChangedCallback cb) {
    on_contact_status_changed_ = std::move(cb);
  }
  void SetContactRemovedCallback(OnContactRemovedCallback cb) {
    on_contact_removed_ = std::move(cb);
  }

 private:
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;

  AccountService* account_service_;
  ProfileService* profile_service_;
  MessagingService* messaging_service_{nullptr};
  ConnectionRequestHandler connection_handler_;

  OnContactRequestCallback on_contact_request_;
  OnContactStatusChangedCallback on_contact_status_changed_;
  OnContactRemovedCallback on_contact_removed_;
};

}  // namespace eillance::services
