#include "eillance/services/contact_service.hpp"

#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

ContactService::ContactService(
    std::shared_ptr<storage::PersistentStorage> storage,
    std::shared_ptr<transport::Transport> transport,
    std::shared_ptr<identity::ContactManager> contact_manager,
    AccountService* account_service,
    ConnectionRequestHandler connection_handler
)
    : storage_(std::move(storage)),
      transport_(std::move(transport)),
      manager_(std::move(contact_manager)),
      account_service_(account_service),
      connection_handler_(std::move(connection_handler)) {}

void ContactService::SetMessagingService(MessagingService* messaging_service) {
  messaging_service_ = messaging_service;
}

void ContactService::EnqueueOrSend(ContactID const& peer_id, nlohmann::json const& msg) {
  storage_->AddPendingAction(peer_id, msg.dump());
  SendPendingActions(peer_id);
}

void ContactService::SendPendingActions(ContactID const& peer_id) {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer || !peer->data_channel_is_open) {
    connection_handler_(peer_id);
    return;
  }

  auto actions = storage_->GetPendingActions(peer_id);
  for (auto const& action : actions) {
    try {
      transport_->SendPeerDataChannelMessage(peer_id, nlohmann::json::parse(action.payload));
      storage_->RemovePendingAction(action.id);
    } catch (...) {
      // Keep in DB if parsing or sending fails? Sending via transport usually doesn't throw.
      // If parsing fails, we should remove it to avoid poison pill loop.
      storage_->RemovePendingAction(action.id);
    }
  }
}

void ContactService::AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) {
  auto const existing = manager_->GetContact(peer_id);
  if (existing && existing->status == identity::ContactStatus::awaitingApproval) {
    ConfirmContact(peer_id);
    return;
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
    messaging_service_->AddSystemEntry(peer_id, content.dump(), messaging::MessageDirection::sent);

    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::pendingApproval);

    // Trigger delivery via messaging service
    connection_handler_(peer_id);
    messaging_service_->SendQueuedMessages(peer_id);
  }
}

void ContactService::ConfirmContact(ContactID const& peer_id) {
  if (manager_->ConfirmContact(peer_id)) {
    INVARIANT(messaging_service_);
    messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "contact_confirm"}}.dump());

    auto signature = account_service_->Sign(peer_id.Span());
    manager_->StoreMySignatureFor(peer_id, signature);

    messaging_service_->SendSystemMessage(
        peer_id,
        nlohmann::json{{"action", "contact_signature"}, {"signature", crypto::ToHex(signature)}}.dump()
    );

    // This system message is just for local history.
    messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());

    // Send our profile and request theirs since the channel is likely open or opening.
    SendMyProfile(peer_id);  // Queue profile
    SyncProfile(peer_id);    // Flush actions

    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

    // Ensure connection and flush queue to send the "contact_confirm" signaling message immediately
    connection_handler_(peer_id);
    messaging_service_->SendQueuedMessages(peer_id);
  }
}

void ContactService::BlockContact(ContactID const& peer_id, bool notify_peer) {
  if (notify_peer) {
    EnqueueOrSend(peer_id, {{"type", "contact_removed"}});
  }

  transport_->ClosePeerConnection(peer_id, "data");
  transport_->ClosePeerConnection(peer_id, "media");
  transport_->RemovePeer(peer_id);

  manager_->BlockContact(peer_id);
}

void ContactService::DeleteContact(ContactID const& peer_id) {
  BlockContact(peer_id, false);
  manager_->DeleteContact(peer_id);
}

void ContactService::UnblockContact(ContactID const& peer_id) {
  if (manager_->UnblockContact(peer_id)) {
    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::awaitingApproval);
  }
}

void ContactService::SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
  manager_->SetDisplayNameOverride(peer_id, name);
  INVARIANT(on_contact_profile_updated_);
  on_contact_profile_updated_(peer_id);
}

void ContactService::ResetContactDisplayNameOverride(ContactID const& peer_id) {
  manager_->ResetDisplayNameOverride(peer_id);
  EnqueueOrSend(peer_id, {{"type", "profile_request"}});
  INVARIANT(on_contact_profile_updated_);
  on_contact_profile_updated_(peer_id);
}

void ContactService::MarkAllContactsProfileSyncNeeded() {
  auto const contacts = manager_->GetContacts();
  for (auto const& contact : contacts) {
    if (contact.status == identity::ContactStatus::confirmed) {
      SendMyProfile(contact.peer_id);
    }
  }
}

void ContactService::SyncProfile(ContactID const& peer_id) { SendPendingActions(peer_id); }

void ContactService::SendMyProfile(ContactID const& peer_id) {
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
}

auto ContactService::GenerateContactToken() -> std::vector<std::byte> {
  auto arr = manager_->GenerateContactToken();
  return {arr.begin(), arr.end()};
}

auto ContactService::GetContacts() const -> std::vector<identity::Contact> { return manager_->GetContacts(); }

auto ContactService::GetContact(ContactID const& peer_id) const -> std::optional<identity::Contact> {
  return manager_->GetContact(peer_id);
}

auto ContactService::IsConfirmed(ContactID const& peer_id) const -> bool {
  return manager_->IsConfirmedContact(peer_id);
}

// --- Event Processing ---

void ContactService::OnContactAddRequest(
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
    } else {
      INVARIANT(on_contact_profile_updated_);
      on_contact_profile_updated_(peer_id);
    }
  } else {
    if (manager_->IsConfirmedContact(peer_id)) {
      transport_->SendSignalingMessage({{"type", "contact_confirm"}, {"to", peer_id.ToString()}});
    }
  }
}

void ContactService::OnContactConfirm(ContactID const& peer_id) {
  if (manager_->MarkAsConfirmed(peer_id)) {
    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

    INVARIANT(messaging_service_);
    messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());
    messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "request_profile"}}.dump());

    // Send our profile and request theirs since the channel is likely open or opening.
    SendMyProfile(peer_id);  // Queue profile
    SyncProfile(peer_id);    // Flush actions
    EnqueueOrSend(peer_id, {{"type", "profile_request"}});
  }
}

void ContactService::OnContactRemoved(ContactID const& peer_id) {
  INVARIANT(on_contact_removed_);
  on_contact_removed_(peer_id);
  BlockContact(peer_id, false);
}

void ContactService::OnProfileUpdate(ContactID const& peer_id, nlohmann::json const& profile_json) {
  auto get_opt = [&](char const* k) -> std::optional<std::string> {
    if (profile_json.contains(k) && !profile_json[k].is_null()) {
      return profile_json[k].get<std::string>();
    }
    return std::nullopt;
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
  INVARIANT(on_contact_profile_updated_);
  on_contact_profile_updated_(peer_id);
}

void ContactService::OnReconfirmRequest(ContactID const& peer_id) {
  if (auto contact = manager_->GetContact(peer_id); contact && contact->my_signature_for_them) {
    nlohmann::json const resp = {
        {"type", "reconfirm_response"},
        {"signature", crypto::ToHex(*contact->my_signature_for_them)}
    };
    EnqueueOrSend(peer_id, resp);
  }
}

void ContactService::OnReconfirmResponse(ContactID const& peer_id, std::string const& signature_hex) {
  auto sig = crypto::FromHex(signature_hex);
  if (crypto::Identity::Verify(peer_id.Span(), account_service_->GetId().Span(), sig)) {
    manager_->MarkAsConfirmed(peer_id);

    auto my_sig = account_service_->Sign(peer_id.Span());
    manager_->StoreMySignatureFor(peer_id, my_sig);

    EnqueueOrSend(peer_id, {{"type", "contact_signature"}, {"signature", crypto::ToHex(my_sig)}});

    INVARIANT(on_contact_status_changed_);
    on_contact_status_changed_(peer_id, identity::ContactStatus::confirmed);

    EnqueueOrSend(peer_id, {{"type", "profile_request"}});
    INVARIANT(messaging_service_);
    messaging_service_->SendSystemMessage(peer_id, nlohmann::json{{"action", "send_profile"}}.dump());

    // Mark sync needed and try to sync
    manager_->ReloadContacts();
    SendMyProfile(peer_id);  // Queue
    SyncProfile(peer_id);    // Flush
  }
}

void ContactService::OnContactSignature(ContactID const& peer_id, std::string const& signature_hex) {
  auto sig = crypto::FromHex(signature_hex);
  if (crypto::Identity::Verify(peer_id.Span(), account_service_->GetId().Span(), sig)) {
    manager_->StoreSignatureFrom(peer_id, sig);
  }
}

void ContactService::OnDeviceProfileReceived(ContactID const& peer_id, nlohmann::json const& profile_json) {
  auto profile = ParseDeviceProfile(profile_json);
  {
    std::lock_guard<std::mutex> const lock(online_peers_mutex_);
    online_peers_[peer_id].id = peer_id.ToString();
    online_peers_[peer_id].profile = profile;
  }
  NotifyPeerListUpdate();
  INVARIANT(on_profile_received_);
  on_profile_received_(peer_id, profile);
}

void ContactService::NotifyPeerListUpdate() const {
  INVARIANT(on_peer_list_update_);
  std::vector<Device> list;
  {
    std::lock_guard<std::mutex> const lock(online_peers_mutex_);
    for (auto const& pair : online_peers_) {
      list.push_back(pair.second);
    }
  }
  on_peer_list_update_(list);
}

auto ContactService::ParseDeviceProfile(nlohmann::json const& jprofile) -> Device::Profile {
  Device::Profile profile;
  for (auto const& cam_item : jprofile) {
    Device::Camera camera;
    camera.id = cam_item.at("id").get<std::string>();
    camera.name = cam_item.at("name").get<std::string>();
    for (auto const& res_item : cam_item.at("resolutions")) {
      camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
    }
    profile.push_back(camera);
  }
  return profile;
}

void ContactService::SetContactRequestCallback(OnContactRequestCallback cb) { on_contact_request_ = std::move(cb); }
void ContactService::SetContactStatusChangedCallback(OnContactStatusChangedCallback cb) {
  on_contact_status_changed_ = std::move(cb);
}
void ContactService::SetContactProfileUpdatedCallback(OnContactProfileUpdatedCallback cb) {
  on_contact_profile_updated_ = std::move(cb);
}
void ContactService::SetContactRemovedCallback(OnContactRemovedCallback cb) { on_contact_removed_ = std::move(cb); }
void ContactService::SetPeerListUpdateCallback(OnPeerListUpdateCallback cb) { on_peer_list_update_ = std::move(cb); }
void ContactService::SetProfileReceivedCallback(OnProfileReceivedCallback cb) { on_profile_received_ = std::move(cb); }

}  // namespace eillance::services
