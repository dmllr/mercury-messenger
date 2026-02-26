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
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class ContactService {
 public:
  explicit ContactService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      std::shared_ptr<AccountService> account_service,
      std::shared_ptr<ProfileService> profile_service
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        manager_(std::move(contact_manager)),
        account_service_(std::move(account_service)),
        profile_service_(std::move(profile_service)) {
  }

  void SetMessagingService(std::shared_ptr<MessagingService> messaging_service) {
    messaging_service_ = std::move(messaging_service);
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

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::pendingApproval});
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

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

      // Ensure connection and flush queue.
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
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

    // Connections are closed by Client based on status change or direct call,
    // but here we mark it blocked. The Client logic handles actual transport teardown
    // typically when status changes to Rejected or via explicit block logic.
    // In strict Separation, ContactService manages Logic, Client manages Transport.
    // However, to ensure immediate effect:
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

    if (name || bio || photo) {
      profile_service_->NotifyProfileUpdated(peer_id);
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

      INVARIANT(messaging_service_);
      nlohmann::json const content = {{"action", "contact_request_received"}};
      messaging_service_->AddSystemEntry(peer_id, content.dump(), messaging::MessageDirection::received);
      messaging_service_->MarkMessagesAsRead(peer_id, {});

      if (status == identity::RequestStatus::New) {
        EventQueue::Instance().Push(EventContactRequest{peer_id});
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

        EventQueue::Instance().Push(EventConnectionRequested{peer_id});
        INVARIANT(messaging_service_);
        messaging_service_->SendQueuedMessages(peer_id);
      }
    }
  }

  void OnContactConfirm(ContactID const& peer_id) {
    if (manager_->MarkAsConfirmed(peer_id)) {
      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

      INVARIANT(messaging_service_);
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());
      messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "request_profile"}}.dump());

      profile_service_->SendMyProfile(peer_id);
      profile_service_->RequestProfile(peer_id);
    }
  }

  void OnContactRestored(ContactID const& peer_id) {
    profile_service_->RequestProfile(peer_id);
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

      EventQueue::Instance().Push(EventContactStatusChanged{peer_id, identity::ContactStatus::confirmed});

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

 private:
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;

  std::shared_ptr<AccountService> account_service_;
  std::shared_ptr<ProfileService> profile_service_;
  std::shared_ptr<MessagingService> messaging_service_{nullptr};
};

}  // namespace eillance::services
