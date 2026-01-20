#include "eillance/client/client.hpp"

#include <stdexcept>
#include <variant>

#include "eillance/rtc/stream_source.hpp"
#include "eillance/utils/memory.hpp"

namespace eillance {

Client::Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport)
    : storage_(std::move(storage)),
      transport_(std::move(transport)) {
  transport_->SetDelegate(this);

  if (auto db_path = storage_->GetPath(); !db_path.empty()) {
    backup_manager_ = std::make_unique<backup::BackupManager>(db_path);
  }

  id_manager_ = std::make_unique<identity::IdManager>(storage_);
  settings_manager_ = std::make_unique<SettingsManager>(storage_);
  contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
  message_history_ = std::make_unique<messaging::MessageHistory>(storage_);

  // Register the status changed callback here to ensure it's always active.
  message_history_->SetStatusChangedCallback(
      [this](ContactID const& peer_id, std::uint64_t msg_id, messaging::MessageStatus status) {
        if (on_message_status_changed_) {
          (*on_message_status_changed_)(peer_id, msg_id, status);
        }
      }
  );

  delivery_service_ =
      std::make_unique<messaging::MessageDeliveryService>(storage_, contact_manager_, [this](ContactID const& peer_id) {
        if (auto peer = transport_->GetPeer(peer_id);
            peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
          SendQueuedMessages(peer_id);
        } else if (!peer) {
          Connect(peer_id);
        }
      });

  transport_->SetStatusCallback([this](ContactID const& peer_id, EventSource const source, int state_val) {
    if (source == EventSource::peerConnectionState) {
      auto const state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      if (state == eillance::transport::PeerConnectionState::failed ||
          state == eillance::transport::PeerConnectionState::closed ||
          state == eillance::transport::PeerConnectionState::disconnected) {
        transport_->ClosePeerConnection(peer_id, "data");
        transport_->ClosePeerConnection(peer_id, "media");
        transport_->RemovePeer(peer_id);
        delivery_service_->ConnectionAttemptFinished(peer_id);
      }
    }
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state_val);
    }
  });
}

Client::~Client() = default;

void Client::InitializeAvStream() {
  if (!video_capture_ || !audio_capture_) {
    return;
  }
  av_stream_ = std::make_shared<Stream>(
      std::make_shared<StreamSource>(std::move(*video_capture_)),
      std::make_shared<StreamSource>(std::move(*audio_capture_))
  );
  av_stream_->OnSample([this](Stream::Source::Type type, uint64_t sample_time, std::vector<std::byte> const& sample) {
    SendSampleToPeers(sample, type, sample_time);
  });
}

auto Client::GetId() const -> ContactID { return ContactID(id_manager_->GetPublicKey()); }

auto Client::IdentityExists() const -> bool { return id_manager_->IdentityExists(); }

auto Client::GenerateNewIdentity() const -> std::string { return id_manager_->GenerateNewIdentity(); }

auto Client::GetMnemonic() const -> std::optional<std::string> { return id_manager_->GetMnemonic(); }

auto Client::RestoreIdentity(std::string const& mnemonic) const -> bool {
  return id_manager_->RestoreFromMnemonic(mnemonic);
}

auto Client::CreateBackup(std::string const& output_path, std::string const& password) const -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->CreateBackup(output_path, password);
}

auto Client::RestoreBackup(std::string const& input_path, std::string const& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->RestoreFromBackup(input_path, password);
}

auto Client::GetStorage() const -> std::shared_ptr<storage::PersistentStorage> { return storage_; }

auto Client::GetSettingsManager() const -> SettingsManager* { return settings_manager_.get(); }

void Client::SetName(std::string const& name) const { transport_->SetName(name); }

void Client::Start() {
  if (!id_manager_->IdentityExists()) {
    LGE << "Client started without an identity.";
    return;
  }
  if (is_running_.exchange(true)) {
    return;
  }

  if (auto url = settings_manager_->GetSignalingServerUrl()) {
    transport_->SetSignalingUrl(*url);
  }

  transport_->SetId(GetId());
  transport_->Start();
  inactivity_check_queue_.Dispatch([this] { CheckForInactivePeers(); });
}

void Client::Terminate() {
  is_running_ = false;
  delivery_service_->Stop();
  transport_->Terminate();
}

void Client::SetStatusCallback(StatusCallbackFunction const& callback) {
  status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
}

void Client::SetFrameReceivedCallback(OnFrameReceivedCallback const& callback) {
  on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback);
}

void Client::SetProfileReceivedCallback(OnProfileReceivedCallback const& callback) {
  on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback);
}

void Client::SetChatMessageReceivedCallback(OnChatMessageReceivedCallback const& callback) {
  on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback);
}

void Client::SetContactRequestCallback(OnContactRequestCallback const& callback) {
  on_contact_request_ = std::make_shared<OnContactRequestCallback>(callback);
}

void Client::SetContactStatusChangedCallback(OnContactStatusChangedCallback const& callback) {
  on_contact_status_changed_ = std::make_shared<OnContactStatusChangedCallback>(callback);
}

void Client::SetMessageStatusChangedCallback(OnMessageStatusChangedCallback const& callback) {
  on_message_status_changed_ = std::make_shared<OnMessageStatusChangedCallback>(callback);
}

void Client::SetPeerListUpdateCallback(OnPeerListUpdateCallback const& callback) {
  on_peer_list_update_ = std::make_shared<OnPeerListUpdateCallback>(callback);
}

void Client::SetIncomingCallCallback(OnIncomingCallCallback const& callback) {
  on_incoming_call_ = std::make_shared<OnIncomingCallCallback>(callback);
}

void Client::SetOnCallRingingCallback(OnCallRingingCallback const& callback) {
  on_call_ringing_ = std::make_shared<OnCallRingingCallback>(callback);
}

void Client::SetOnCallAcceptedCallback(OnCallAcceptedCallback const& callback) {
  on_call_accepted_ = std::make_shared<OnCallAcceptedCallback>(callback);
}

void Client::SetOnCallTerminatedCallback(OnCallTerminatedCallback const& callback) {
  on_call_terminated_ = std::make_shared<OnCallTerminatedCallback>(callback);
}

void Client::SetOnMyProfileUpdatedCallback(OnMyProfileUpdatedCallback const& callback) {
  on_my_profile_updated_ = std::make_shared<OnMyProfileUpdatedCallback>(callback);
}

void Client::SetOnContactRemovedCallback(OnContactRemovedCallback const& callback) {
  on_contact_removed_ = std::make_shared<OnContactRemovedCallback>(callback);
}

void Client::SetOnContactProfileUpdatedCallback(OnContactProfileUpdatedCallback const& callback) {
  on_contact_profile_updated_ = std::make_shared<OnContactProfileUpdatedCallback>(callback);
}

void Client::Call(ContactID const& peer_id) { Connect(peer_id); }

void Client::Connect(ContactID const& peer_id) {
  if (transport_->GetPeer(peer_id)) {
    LGW << "Connect called for peer that already exists: " << peer_id.ToString();
    return;
  }
  auto const peer = std::make_shared<Peer>(peer_id);
  transport_->AddPeer(peer_id, peer);
  SetupDataChannel(peer, peer_id);
  transport_->CreateDataPeerConnection(peer_id);
  transport_->CreateDataChannel(peer_id);
  transport_->CreateOffer(peer_id, "data");
}

void Client::StartVoiceCall(ContactID const& peer_id) { StartMediaCall(peer_id, false); }

void Client::StartVideoCall(ContactID const& peer_id) { StartMediaCall(peer_id, true); }

void Client::AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind) const {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer) {
    LGW << "Cannot accept call from " << peer_id.ToString() << ", peer object not found.";
    return;
  }

  Json const msg = {{"action", "call_accept"}, {"kind", call_kind}};
  message_history_->AddTextEntry(
      peer_id,
      msg.dump(),
      messaging::MessageDirection::sent,
      messaging::HistoryEntryKind::systemMessage
  );
  delivery_service_->AttemptDelivery(peer_id);

  if (on_call_accepted_) {
    (*on_call_accepted_)(peer_id);
  }
}

void Client::RejectCall(ContactID const& peer_id) const { Hangup(peer_id, true); }

void Client::AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) const {
  auto const existing_contact = contact_manager_->GetContact(peer_id);
  if (existing_contact && existing_contact->status == identity::ContactStatus::awaitingApproval) {
    ConfirmContact(peer_id);
    return;
  }
  if (contact_manager_->AddContact(peer_id)) {
    Json content = {
        {"action", "contact_request"},
        {"profile",
         {{"display_name", settings_manager_->GetMyDisplayName().value_or("")},
          {"bio", settings_manager_->GetMyBio().value_or("")},
          {"photo", settings_manager_->GetMyPhotoBase64()}}},
    };
    if (token) {
      content["token"] = crypto::ToHex(*token);
    }
    message_history_->AddTextEntry(
        peer_id,
        content.dump(),
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );
    if (on_contact_status_changed_) {
      (*on_contact_status_changed_)(peer_id, identity::ContactStatus::pendingApproval);
    }
    delivery_service_->AttemptDelivery(peer_id);
  }
}

auto Client::GenerateContactToken() -> std::vector<std::byte> {
  auto token_bytes = contact_manager_->GenerateContactToken();
  return {token_bytes.begin(), token_bytes.end()};
}

void Client::DeleteContact(ContactID const& peer_id) const {
  if (auto peer = transport_->GetPeer(peer_id)) {
    transport_->ClosePeerConnection(peer_id, "data");
    transport_->ClosePeerConnection(peer_id, "media");
    transport_->RemovePeer(peer_id);
  }
  contact_manager_->DeleteContact(peer_id);
}

void Client::BlockContact(ContactID const& peer_id, bool notify_peer) const {
  if (notify_peer) {
    if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      Json const msg = {{"type", "contact_removed"}};
      transport_->SendPeerDataChannelMessage(peer_id, msg);
    }
  }

  if (auto peer = transport_->GetPeer(peer_id)) {
    transport_->ClosePeerConnection(peer_id, "data");
    transport_->ClosePeerConnection(peer_id, "media");
    transport_->RemovePeer(peer_id);
  }
  contact_manager_->BlockContact(peer_id);
}

void Client::UnblockContact(ContactID const& peer_id) const {
  if (contact_manager_->UnblockContact(peer_id)) {
    if (on_contact_status_changed_) {
      (*on_contact_status_changed_)(peer_id, identity::ContactStatus::awaitingApproval);
    }
  }
}

void Client::ConfirmContact(ContactID const& peer_id) const {
  if (contact_manager_->ConfirmContact(peer_id)) {
    Json const content = {{"action", "contact_confirm"}};
    message_history_->AddTextEntry(
        peer_id,
        content.dump(),
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );

    auto signature_bytes = id_manager_->Sign(peer_id.Span());
    contact_manager_->StoreMySignatureFor(peer_id, signature_bytes);

    Json const sig_content = {{"action", "contact_signature"}, {"signature", crypto::ToHex(signature_bytes)}};
    message_history_->AddTextEntry(
        peer_id,
        sig_content.dump(),
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );

    Json const profile_content = {{"action", "send_profile"}};
    message_history_->AddTextEntry(
        peer_id,
        profile_content.dump(),
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );

    if (on_contact_status_changed_) {
      (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
    }
    delivery_service_->AttemptDelivery(peer_id);
  }
}

auto Client::GetPeerConnectionState(ContactID const& peer_id) const -> eillance::transport::PeerConnectionState {
  return transport_->GetPeerConnectionState(peer_id);
}

auto Client::GetContacts() const -> std::vector<identity::Contact> { return contact_manager_->GetContacts(); }

auto Client::GetChatSummaries(std::vector<ContactID> const& peer_ids) const -> std::vector<messaging::ChatSummary> {
  return message_history_->GetChatSummaries(peer_ids);
}

void Client::Hangup(ContactID const& peer_id, bool notify_peer) const {
  if (auto const peer = transport_->GetPeer(peer_id); peer != nullptr) {
    peer->pending_offer_sdp.reset();
    if (!peer->pending_candidates.empty()) {
      peer->pending_candidates.clear();
    }

    if (peer->has_media_peer_connection) {
      transport_->ClosePeerConnection(peer_id, "media");
      peer->has_media_peer_connection = false;
      peer->audio = nullptr;
      peer->video = nullptr;
      if (av_stream_) {
        av_stream_->Stop();
      }
    }

    if (notify_peer && peer->data_channel_is_open) {
      Json const msg = {{"type", "hangup"}};
      transport_->SendPeerDataChannelMessage(peer_id, msg);
    }
  }
  if (on_call_terminated_) {
    (*on_call_terminated_)(peer_id);
  }
}

void Client::Configure(Config const& config) const {
  if (av_stream_) {
    av_stream_->Video()->Configure(config);
    av_stream_->Audio()->Configure(config);
  }
}

auto Client::SendChatMessage(ContactID const& peer_id, std::string const& message) -> messaging::HistoryEntryRecord {
  if (!contact_manager_->GetContact(peer_id)) {
    LGE << "Cannot send message to " << peer_id.ToString() << ". Not a contact.";
    return {};
  }
  auto entry =
      message_history_
          ->AddTextEntry(peer_id, message, messaging::MessageDirection::sent, messaging::HistoryEntryKind::textMessage);

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }

  return entry;
}

void Client::UpdateSignalingUrl(std::string const& url) const {
  if (is_running_) {
    settings_manager_->SetSignalingServerUrl(url);
    transport_->SetSignalingUrl(url);
    transport_->Reconnect();
  } else {
    settings_manager_->SetSignalingServerUrl(url);
  }
}

void Client::MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) const {
  auto const peer = transport_->GetPeer(peer_id);
  message_history_->MarkMessagesAsRead(peer_id, message_ids);
  if (peer && peer->data_channel_is_open) {
    Json const msg = {
        {"type", "message_read"},
        {"message_ids", message_ids},
    };
    transport_->SendPeerDataChannelMessage(peer_id, msg);
  }
}

auto Client::GetMessageHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
    -> messaging::HistoryPage {
  return message_history_->GetHistoryPage(peer_id, page_index, page_size);
}

auto Client::GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string> {
  return message_history_->GetFullMessageContent(message_id);
}

void Client::UpdateMyProfile(
    std::optional<std::string> const& name,
    std::optional<std::string> const& bio,
    std::optional<std::string> const& photo
) {
  if (name) {
    settings_manager_->SetMyDisplayName(*name);
  }
  if (bio) {
    settings_manager_->SetMyBio(*bio);
  }
  if (photo) {
    settings_manager_->SetMyPhotoBase64(*photo);
  }

  if (on_my_profile_updated_) {
    (*on_my_profile_updated_)();
  }

  for (auto const& contact : contact_manager_->GetContacts()) {
    if (contact.status == identity::ContactStatus::confirmed) {
      Json profile_update_msg;
      profile_update_msg["action"] = "profile_update";
      profile_update_msg["display_name"] = settings_manager_->GetMyDisplayName().value_or("");
      profile_update_msg["bio"] = settings_manager_->GetMyBio().value_or("");
      profile_update_msg["photo"] = settings_manager_->GetMyPhotoBase64();

      ContactID const id(contact.peer_id);
      message_history_->AddTextEntry(
          id,
          profile_update_msg.dump(),
          messaging::MessageDirection::sent,
          messaging::HistoryEntryKind::systemMessage
      );

      if (auto peer = transport_->GetPeer(id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
        SendQueuedMessages(id);
      } else {
        delivery_service_->AttemptDelivery(id);
      }
    }
  }
}

void Client::SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& override_name) const {
  contact_manager_->SetDisplayNameOverride(peer_id, override_name);
  if (on_contact_profile_updated_) {
    (*on_contact_profile_updated_)(peer_id);
  }
}

void Client::ResetContactDisplayNameOverride(ContactID const& peer_id) const {
  contact_manager_->ResetDisplayNameOverride(peer_id);
  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
    Json const request = {{"type", "profile_request"}};
    transport_->SendPeerDataChannelMessage(peer_id, request);
  }
  if (on_contact_profile_updated_) {
    (*on_contact_profile_updated_)(peer_id);
  }
}

void Client::DebugEraseAllHistory() const { storage_->DebugClearAllHistory(); }

void Client::DebugEraseAllContacts() const {
  storage_->DebugClearAllContacts();
  storage_->DebugClearAllHistory();
}

void Client::DebugRevokeAllApprovals() const { storage_->DebugRevokeAllContactApprovals(); }

void Client::OnPeerReady(std::shared_ptr<Peer> const&) {}

auto Client::Sign(std::span<std::byte const> message) -> std::vector<std::byte> { return id_manager_->Sign(message); }

void Client::OnLoginSuccess() { delivery_service_->Start(); }

void Client::OnSystemMessage(std::string const& type, Json const& message) {
  if (type == "auth_challenge") {
    auto const challenge = message.value("challenge", "");
    if (!challenge.empty()) {
      // NOLINTNEXTLINE(*-reinterpret-cast)
      std::span<std::byte const> const challenge_bytes(
          reinterpret_cast<std::byte const*>(challenge.data()),
          challenge.size()
      );
      auto const signature_bytes = Sign(challenge_bytes);
      auto const signature_hex = crypto::ToHex(signature_bytes);
      Json const response = {{"type", "auth_response"}, {"id", GetId().ToString()}, {"signature", signature_hex}};
      transport_->SendSignalingMessage(response);
    }
  } else if (type == "login_success") {
    OnLoginSuccess();
    if (status_callback_) {
      (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
    }
  } else {
    LGW << "Received unknown system message of type: " << type;
  }
}

void Client::OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) {
  if (error == "peer_not_found") {
    LGW << "Connection attempt failed for peer " << peer_id.ToString() << " not online.";
    delivery_service_->ConnectionAttemptFinished(peer_id);
    if (transport_->GetPeer(peer_id)) {
      transport_->ClosePeerConnection(peer_id, "data");
    }
  }
}

void Client::OnDataChannelOpen(ContactID const& peer_id) {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  }
  UpdatePeerActivity(peer_id);
  LGI << "Data channel with " << peer_id.ToString() << " opened.";

  peer->session_cipher->Reset();
  peer->data_channel_is_open = true;

  if (GetId() > peer_id) {
    auto my_pub_key = peer->session_cipher->GetPublicKey();
    Json const key_exchange_message = {
        {"type", "key_exchange"},
        {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
        {"is_reply", false}
    };
    transport_->SendPeerDataChannelMessage(peer_id, key_exchange_message);
  }
}

void Client::OnFrameReceived(ContactID const& /*peer_id*/, std::vector<std::byte> const& frame) {
  if (on_frame_received_) {
    (*on_frame_received_)(frame);
  }
}

void Client::StartMediaCall(ContactID const& peer_id, bool const is_video) {
  auto const call_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

  Json const msg = {{"action", "call_request"}, {"kind", call_kind}};
  message_history_->AddTextEntry(
      peer_id,
      msg.dump(),
      messaging::MessageDirection::sent,
      messaging::HistoryEntryKind::systemMessage
  );

  auto peer = transport_->GetPeer(peer_id);
  if (peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }
}

void Client::UpdateAndNotifyPeerList() {
  if (on_peer_list_update_) {
    std::vector<Device> peer_list;
    {
      std::lock_guard<std::mutex> const lock(online_peers_mutex_);
      for (auto const& pair : online_peers_) {
        peer_list.push_back(pair.second);
      }
    }
    (*on_peer_list_update_)(peer_list);
  }
}

void Client::SendSampleToPeers(
    std::vector<std::byte> const& sample,
    Stream::Source::Type type,
    std::uint64_t sample_time
) const {
  for (auto const& [id, peer] : transport_->GetPeers()) {
    if (peer->GetState() == Peer::State::ready) {
      auto track = (type == Stream::Source::Type::video) ? peer->video : peer->audio;
      if (track) {
        track->SendSample(sample, sample_time);
      }
    }
  }
}

void Client::ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
  UpdatePeerActivity(peer_id);
  auto const itr = message.find("type");
  if (itr == message.end()) {
    return;
  }
  auto const type = itr->get<std::string>();
  auto const peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  };

  if (type == "key_exchange") {
    auto pub_key_bytes = message.at("key").get<std::vector<std::byte>>();
    if (peer->session_cipher->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
      LGI << "Successfully computed shared secret with peer " << peer_id.ToString();
      bool const is_reply = message.value("is_reply", false);
      if (!is_reply) {
        auto my_pub_key = peer->session_cipher->GetPublicKey();
        Json const reply = {
            {"type", "key_exchange"},
            {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
            {"is_reply", true}
        };
        transport_->SendPeerDataChannelMessage(peer_id, reply);
      } else {
        SendQueuedMessages(peer_id);
      }
    } else {
      LGE << "Failed to compute shared secret with peer " << peer_id.ToString();
    }
    return;
  }

  if (type == "hangup") {
    LGI << "Received hangup signal from " << peer_id.ToString();
    Hangup(peer_id, false);
    return;
  }

  if (type == "reconfirm_request") {
    auto const contact_info = contact_manager_->GetContact(peer_id);
    if (contact_info && contact_info->my_signature_for_them) {
      Json const reconfirm_response = {
          {"type", "reconfirm_response"},
          {"signature", crypto::ToHex(*contact_info->my_signature_for_them)}
      };
      transport_->SendPeerDataChannelMessage(peer_id, reconfirm_response);
    }
    return;
  }

  if (type == "reconfirm_response") {
    auto const signature_hex = message.at("signature").get<std::string>();
    auto const signature_bytes = crypto::FromHex(signature_hex);

    if (crypto::Identity::Verify(peer_id.Span(), GetId().Span(), signature_bytes)) {
      contact_manager_->MarkAsConfirmed(peer_id);

      auto const my_signature_bytes = id_manager_->Sign(peer_id.Span());
      auto const my_signature_hex = crypto::ToHex(my_signature_bytes);
      contact_manager_->StoreMySignatureFor(peer_id, my_signature_bytes);

      Json const sig_content = {{"type", "contact_signature"}, {"signature", my_signature_hex}};
      transport_->SendPeerDataChannelMessage(peer_id, sig_content);

      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
      }

      Json const request_profile = {{"type", "profile_request"}};
      transport_->SendPeerDataChannelMessage(peer_id, request_profile);
      Json const send_profile = {{"action", "send_profile"}};
      message_history_->AddTextEntry(
          peer_id,
          send_profile.dump(),
          messaging::MessageDirection::sent,
          messaging::HistoryEntryKind::systemMessage
      );
      delivery_service_->AttemptDelivery(peer_id);
    }
    return;
  }

  if (type == "contact_signature") {
    auto signature_hex = message.at("signature").get<std::string>();
    auto signature_bytes = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.Span(), GetId().Span(), signature_bytes)) {
      contact_manager_->StoreSignatureFrom(peer_id, signature_bytes);
    }
    return;
  }

  if (!contact_manager_->IsConfirmedContact(peer_id)) {
    Json const reconfirm_request = {{"type", "reconfirm_request"}};
    transport_->SendPeerDataChannelMessage(peer_id, reconfirm_request);
    return;
  }

  if (type == "profile") {
    auto const profile = ParseDeviceProfile(message.at("profile"));

    {
      std::lock_guard<std::mutex> const lock(online_peers_mutex_);
      online_peers_[peer_id].id = peer_id.ToString();
      online_peers_[peer_id].profile = profile;
      if (message.contains("name")) {
        online_peers_[peer_id].name = message.at("name").get<std::string>();
      }
    }
    UpdateAndNotifyPeerList();
    if (on_profile_received_) {
      (*on_profile_received_)(peer_id, profile);
    }
  } else if (type == "termination") {
    transport_->RemovePeer(peer_id);
  } else if (type == "chat") {
    if (!peer->session_cipher->IsReady()) {
      return;
    }
    auto message_id = message.at("message_id").get<std::uint64_t>();
    bool const is_system = message.value("is_system", false);
    bool message_processed_successfully = false;

    if (is_system) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        message_processed_successfully = true;
        auto const content_json = Json::parse(decrypted_message);
        auto const action = content_json.value("action", "");
        if (action == "contact_signature") {
          auto signature_hex = content_json.at("signature").get<std::string>();
          auto signature_bytes = crypto::FromHex(signature_hex);
          if (crypto::Identity::Verify(peer_id.Span(), GetId().Span(), signature_bytes)) {
            contact_manager_->StoreSignatureFrom(peer_id, signature_bytes);
            auto my_contact_info_for_them = contact_manager_->GetContact(peer_id);
            if (!my_contact_info_for_them || !my_contact_info_for_them->my_signature_for_them) {
              auto my_signature_bytes = id_manager_->Sign(peer_id.Span());
              auto my_signature_hex = crypto::ToHex(my_signature_bytes);
              contact_manager_->StoreMySignatureFor(peer_id, my_signature_bytes);
              Json const sig_content = {{"action", "contact_signature"}, {"signature", my_signature_hex}};
              message_history_->AddTextEntry(
                  peer_id,
                  sig_content.dump(),
                  messaging::MessageDirection::sent,
                  messaging::HistoryEntryKind::systemMessage
              );
              delivery_service_->AttemptDelivery(peer_id);
            }
          }
        } else if (action == "call_request") {
          if (on_incoming_call_) {
            messaging::HistoryEntryKind const call_type =
                content_json.value("kind", messaging::HistoryEntryKind::voiceCall);
            (*on_incoming_call_)(peer_id, call_type);

            Json const ringing_msg = {{"action", "call_ringing"}};
            message_history_->AddTextEntry(
                peer_id,
                ringing_msg.dump(),
                messaging::MessageDirection::sent,
                messaging::HistoryEntryKind::systemMessage
            );
            delivery_service_->AttemptDelivery(peer_id);
          }
        } else if (action == "call_ringing") {
          if (on_call_ringing_) {
            (*on_call_ringing_)(peer_id);
          }
        } else if (action == "call_accept") {
          if (on_call_accepted_) {
            (*on_call_accepted_)(peer_id);
          }
          auto const contact = contact_manager_->GetContact(peer_id);
          if (contact && contact->status == identity::ContactStatus::confirmed) {
            bool const is_video = content_json.value("kind", messaging::HistoryEntryKind::voiceCall) ==
                                  messaging::HistoryEntryKind::videoCall;
            if (av_stream_) {
              av_stream_->Start();
            }
            auto accepted_peer = transport_->GetPeer(peer_id);
            if (!accepted_peer) {
              return;
            }
            transport_->CreateMediaPeerConnection(peer_id);
            accepted_peer->has_media_peer_connection = true;
            accepted_peer->audio = transport_->AddAudio(peer_id, [this, wpeer = MakeWeakPtr(accepted_peer)]() {
              if (auto const p = wpeer.lock()) {
                transport_->AddToStream(p, false);
              }
            });
            if (is_video) {
              accepted_peer->video = transport_->AddVideo(peer_id, [this, wpeer = MakeWeakPtr(accepted_peer)]() {
                if (auto const p = wpeer.lock()) {
                  transport_->AddToStream(p, true);
                }
              });
            }
            transport_->CreateOffer(peer_id, "media");
          }
        } else if (action == "profile_update") {
          HandleProfileUpdate(peer_id, content_json);
        }
      }
    } else if (on_chat_message_received_) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        message_processed_successfully = true;
        auto history_entry = message_history_->AddTextEntry(
            peer_id,
            decrypted_message,
            messaging::MessageDirection::received,
            messaging::HistoryEntryKind::textMessage
        );
        (*on_chat_message_received_)(peer_id, history_entry);
      }
    }

    if (message_processed_successfully) {
      Json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      transport_->SendPeerDataChannelMessage(peer_id, receipt);
      SendQueuedMessages(peer_id);
    }

  } else if (type == "message_receipt") {
    auto message_id = message.at("message_id").get<std::uint64_t>();
    auto status_str = message.at("status").get<std::string>();
    auto status = messaging::MessageStatus::sent;
    if (status_str == "delivered") {
      status = messaging::MessageStatus::delivered;
    } else if (status_str == "read") {
      status = messaging::MessageStatus::read;
    }
    message_history_->UpdateMessageStatus(peer_id, message_id, status);
  } else if (type == "message_read") {
    auto const message_ids = message.at("message_ids").get<std::vector<std::uint64_t>>();
    for (auto const msg_id : message_ids) {
      message_history_->UpdateMessageStatus(peer_id, msg_id, messaging::MessageStatus::read);
    }
  } else if (type == "profile_info") {
    HandleProfileUpdate(peer_id, message);
  } else if (type == "profile_request") {
    Json profile_msg;
    profile_msg["type"] = "profile_info";
    profile_msg["display_name"] = settings_manager_->GetMyDisplayName().value_or("");
    profile_msg["bio"] = settings_manager_->GetMyBio().value_or("");

    auto my_photo = settings_manager_->GetMyPhotoBase64();
    if (my_photo && !my_photo->empty()) {
      profile_msg["photo"] = *my_photo;
    } else {
      profile_msg["photo"] = nullptr;
    }
    transport_->SendPeerDataChannelMessage(peer_id, profile_msg);
  } else if (type == "contact_removed") {
    if (on_contact_removed_) {
      (*on_contact_removed_)(peer_id);
    }
    BlockContact(peer_id, false);
  } else {
    throw std::runtime_error("Unknown message type in ProcessDataChannelMessage");
  }
}

void Client::SendQueuedMessages(ContactID const& peer_id) {
  auto const messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  if (messages.empty()) {
    delivery_service_->ConnectionAttemptFinished(peer_id);
    return;
  }
  auto const peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  }
  LGI << "Processing " << messages.size() << " queued message(s) for " << peer_id.ToString();
  for (auto const& entry : messages) {
    bool sent = false;
    switch (entry.kind) {
      case messaging::HistoryEntryKind::textMessage:
        [[fallthrough]];
      case messaging::HistoryEntryKind::systemMessage: {
        if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          UpdatePeerActivity(peer_id);
          continue;
        }

        std::string content_to_send;
        auto const& msg_entry = std::get<messaging::MessageEntry>(entry.data);
        if (entry.is_truncated) {
          auto full_content = message_history_->GetFullMessageContent(entry.id);
          if (full_content) {
            content_to_send = *full_content;
          } else {
            continue;
          }
        } else {
          content_to_send = msg_entry.content;
        }

        if (entry.kind == messaging::HistoryEntryKind::textMessage) {
          if (peer->data_channel_is_open && peer->session_cipher->IsReady()) {
            auto encrypted_payload = peer->session_cipher->Encrypt(content_to_send);
            UpdatePeerActivity(peer_id);
            if (encrypted_payload.empty()) {
              continue;
            }
            Json const json_message = {
                {"type", "chat"},
                {"message_id", entry.id},
                {"is_system", false},
                {"payload", std::vector<std::byte>(encrypted_payload.begin(), encrypted_payload.end())}
            };
            transport_->SendPeerDataChannelMessage(peer_id, json_message);
            sent = true;
          }
        } else {  // systemMessage
          try {
            Json content_json = Json::parse(content_to_send);
            std::string const action = content_json.value("action", "");
            if (action == "contact_request" || action == "contact_confirm") {
              Json msg = {
                  {"type", action == "contact_request" ? "contact_add_request" : "contact_confirm"},
                  {"to", peer_id.ToString()}
              };
              if (action == "contact_request") {
                if (content_json.contains("profile")) {
                  msg["profile"] = content_json["profile"];
                }
                if (content_json.contains("token")) {
                  msg["token"] = content_json["token"];
                }
              }
              transport_->SendSignalingMessage(msg);
              sent = true;
            } else if (action == "profile_update" || action == "contact_signature" || action == "call_accept" ||
                       action == "call_request" || action == "call_ringing") {
              if (peer->data_channel_is_open && peer->session_cipher->IsReady()) {
                auto encrypted_payload = peer->session_cipher->Encrypt(content_to_send);
                UpdatePeerActivity(peer_id);
                if (encrypted_payload.empty()) {
                  continue;
                }
                Json const json_message = {
                    {"type", "chat"},
                    {"message_id", entry.id},
                    {"is_system", true},
                    {"payload", std::vector<std::byte>(encrypted_payload.begin(), encrypted_payload.end())}
                };
                transport_->SendPeerDataChannelMessage(peer_id, json_message);
                sent = true;
              }
            } else if (action == "request_profile") {
              if (peer->data_channel_is_open && peer->session_cipher->IsReady()) {
                UpdatePeerActivity(peer_id);
                Json const request = {{"type", "profile_request"}};
                transport_->SendPeerDataChannelMessage(peer_id, request);
                sent = true;
              }
            } else if (action == "send_profile") {
              if (peer->data_channel_is_open && peer->session_cipher->IsReady()) {
                Json profile_msg;
                UpdatePeerActivity(peer_id);
                profile_msg["type"] = "profile_info";
                profile_msg["display_name"] = settings_manager_->GetMyDisplayName().value_or("");
                profile_msg["bio"] = settings_manager_->GetMyBio().value_or("");
                auto my_photo = settings_manager_->GetMyPhotoBase64();
                if (my_photo && !my_photo->empty()) {
                  profile_msg["photo"] = *my_photo;
                } else {
                  profile_msg["photo"] = nullptr;
                }
                transport_->SendPeerDataChannelMessage(peer_id, profile_msg);
                sent = true;
              }
            }
          } catch (Json::parse_error const& e) {
            LGE << "Failed to parse system message content for id " << entry.id << ": " << e.what();
          }
        }
        break;
      }
      default:
        break;
    }
    if (sent) {
      message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
    }
  }
  delivery_service_->ConnectionAttemptFinished(peer_id);
}

void Client::ProcessPeerMessage(Json message) {
  auto const from_it = message.find("from");
  if (from_it == message.end()) {
    return;
  }
  ContactID const peer_id(from_it->get<std::string>());
  auto const type_it = message.find("type");
  if (type_it == message.end()) {
    return;
  }
  auto type = type_it->get<std::string>();
  auto const context = message.value("context", "data");

  if (context == "media") {
    auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      LGI << "Received media message for unknown peer " << peer_id.ToString() << ". Creating peer object first.";
      peer = std::make_shared<Peer>(peer_id);
      transport_->AddPeer(peer_id, peer);
    }
    if (type == "offer") {
      auto const sdp = message["sdp"].get<std::string>();
      bool const is_video_call = sdp.find("m=video") != std::string::npos;
      if (av_stream_) {
        av_stream_->Start();
      }
      if (!peer->has_media_peer_connection) {
        transport_->CreateMediaPeerConnection(peer_id);
        peer->has_media_peer_connection = true;
      }
      peer->audio = transport_->AddAudio(peer_id, [this, wpeer = MakeWeakPtr(peer)]() {
        if (auto const p = wpeer.lock()) {
          transport_->AddToStream(p, false);
        }
      });
      if (is_video_call) {
        peer->video = transport_->AddVideo(peer_id, [this, wpeer = MakeWeakPtr(peer)]() {
          if (auto const p = wpeer.lock()) {
            transport_->AddToStream(p, true);
          }
        });
      }
      transport_->SetRemoteDescription(peer_id, "media", sdp, "offer");
      transport_->CreateAnswer(peer_id, "media");
    } else if (type == "candidate") {
      auto const candidate_str = message["candidate"].get<std::string>();
      auto const mid = message["sdpMid"].get<std::string>();
      transport_->AddRemoteCandidate(peer_id, "media", candidate_str, mid);
    } else if (peer->has_media_peer_connection && type == "answer") {
      auto const sdp = message["sdp"].get<std::string>();
      transport_->SetRemoteDescription(peer_id, "media", sdp, type);
    }
    return;
  }

  if (type == "offer") {
    auto peer = transport_->GetPeer(peer_id);
    if (peer) {
      if (GetId() > peer_id) {
        transport_->ClosePeerConnection(peer_id, "data");
        transport_->RemovePeer(peer_id);
        peer = nullptr;
      } else {
        return;
      }
    }

    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      transport_->AddPeer(peer_id, peer);
      SetupDataChannel(peer, peer_id);
      transport_->CreateDataPeerConnection(peer_id);
      auto const sdp = message["sdp"].get<std::string>();
      transport_->SetRemoteDescription(peer_id, "data", sdp, "offer");
      transport_->CreateAnswer(peer_id, "data");
    }
  } else if (type == "answer") {
    auto const sdp = message["sdp"].get<std::string>();
    transport_->SetRemoteDescription(peer_id, "data", sdp, "answer");
  } else if (type == "candidate") {
    auto const candidate = message["candidate"].get<std::string>();
    auto const mid = message["sdpMid"].get<std::string>();
    transport_->AddRemoteCandidate(peer_id, "data", candidate, mid);
  } else if (type == "contact_add_request") {
    std::optional<std::string> token;
    if (message.contains("token") && message.at("token").is_string()) {
      token = message.at("token").get<std::string>();
    }

    std::optional<std::string> name;
    std::optional<std::string> bio;
    std::optional<std::string> photo_base64;
    if (message.contains("profile")) {
      auto const& profile = message["profile"];
      if (profile.contains("display_name") && profile["display_name"].is_string()) {
        name = profile["display_name"].get<std::string>();
      }
      if (profile.contains("bio") && profile["bio"].is_string()) {
        bio = profile["bio"].get<std::string>();
      }
      if (profile.contains("photo") && profile["photo"].is_string()) {
        photo_base64 = profile["photo"].get<std::string>();
      }
    }

    auto result = contact_manager_->ReceiveContactRequest(peer_id, name, bio, photo_base64);

    if (result == identity::RequestStatus::New || result == identity::RequestStatus::duplicate) {
      bool valid_token = false;
      if (token) {
        auto token_bytes = crypto::FromHex(*token);
        if (contact_manager_->ValidateAndConsumeContactToken(token_bytes)) {
          valid_token = true;
        }
      }

      if (valid_token) {
        ConfirmContact(peer_id);
        return;
      }

      Json const content = {{"action", "contact_request_received"}};
      auto entry = message_history_->AddTextEntry(
          peer_id,
          content.dump(),
          messaging::MessageDirection::received,
          messaging::HistoryEntryKind::systemMessage
      );
      message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::read);

      if (result == identity::RequestStatus::New) {
        if (on_contact_request_) {
          (*on_contact_request_)(peer_id);
        }
      } else {
        if (on_contact_profile_updated_) {
          (*on_contact_profile_updated_)(peer_id);
        }
      }
    } else {
      if (contact_manager_->IsConfirmedContact(peer_id)) {
        Json const msg = {{"type", "contact_confirm"}, {"to", peer_id.ToString()}};
        transport_->SendSignalingMessage(msg);
      }
    }
  } else if (type == "contact_confirm") {
    if (contact_manager_->MarkAsConfirmed(peer_id)) {
      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
      }

      auto pending_requests = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sent);
      for (auto const& req : pending_requests) {
        if (req.kind == messaging::HistoryEntryKind::systemMessage) {
          auto const& content_str = std::get<messaging::MessageEntry>(req.data).content;
          try {
            Json const content_json = Json::parse(content_str);
            if (content_json.value("action", "") == "contact_request") {
              message_history_->UpdateMessageStatus(peer_id, req.id, messaging::MessageStatus::read);
              break;
            }
          } catch (...) {
          }
        }
      }

      Json const profile_content = {{"action", "send_profile"}};
      message_history_->AddTextEntry(
          peer_id,
          profile_content.dump(),
          messaging::MessageDirection::sent,
          messaging::HistoryEntryKind::systemMessage
      );

      Json const request_content = {{"action", "request_profile"}};
      message_history_->AddTextEntry(
          peer_id,
          request_content.dump(),
          messaging::MessageDirection::sent,
          messaging::HistoryEntryKind::systemMessage
      );

      delivery_service_->AttemptDelivery(peer_id);
    }
  }
}

void Client::CheckForInactivePeers() {
  if (!is_running_) {
    return;
  }

  auto peers = transport_->GetPeers();
  auto const now = Time::Now();
  auto const timeout_s = settings_manager_->GetPeerInactivityTimeout();
  auto const timeout_us = std::chrono::duration_cast<std::chrono::microseconds>(timeout_s).count();

  for (auto const& [peer_id, peer] : peers) {
    if (!peer->has_media_peer_connection &&
        GetPeerConnectionState(peer_id) == transport::PeerConnectionState::connected) {
      if ((now - peer->last_activity_time.load()) > timeout_us) {
        LGI << "Peer " << peer_id.ToString() << " inactive for too long. Closing data connection.";
        transport_->ClosePeerConnection(peer_id, "data");
      }
    }
  }
  inactivity_check_queue_.DispatchAfter(peerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
}

void Client::UpdatePeerActivity(ContactID const& peer_id) const {
  if (auto peer = transport_->GetPeer(peer_id)) {
    peer->last_activity_time = Time::Now();
  }
}

void Client::SetupDataChannel(std::shared_ptr<Peer> const& peer, ContactID const& peer_id) {
  peer->on_data_channel_message = [this, peer_id](std::string const& msg) {
    ProcessDataChannelMessage(peer_id, Json::parse(msg));
  };
}

void Client::HandleProfileUpdate(ContactID const& peer_id, Json const& profile_json) const {
  auto get_optional_from_json = [](Json const& j, char const* key) -> std::optional<std::string> {
    if (j.contains(key)) {
      if (j[key].is_null()) {
        return std::string("");
      }
      return j[key].get<std::string>();
    }
    return std::nullopt;
  };

  auto display_name = get_optional_from_json(profile_json, "display_name");
  auto bio = get_optional_from_json(profile_json, "bio");
  auto photo = get_optional_from_json(profile_json, "photo");

  auto old_contact_opt = contact_manager_->GetContact(peer_id);

  bool const name_changed = display_name && (!old_contact_opt || old_contact_opt->display_name != *display_name);
  bool const bio_changed = bio && (!old_contact_opt || old_contact_opt->bio != *bio);
  bool const photo_changed = photo && (!old_contact_opt || old_contact_opt->photo_base64 != *photo);

  if (name_changed || bio_changed || photo_changed) {
    Json diff_json;
    diff_json["action"] = "profile_update_diff";
    diff_json["new"]["display_name"] = display_name;
    diff_json["new"]["bio"] = bio;
    diff_json["new"]["photo"] = photo;

    if (old_contact_opt) {
      diff_json["old"]["display_name"] = old_contact_opt->display_name;
      diff_json["old"]["bio"] = old_contact_opt->bio;
      diff_json["old"]["photo"] = old_contact_opt->photo_base64;
    } else {
      diff_json["old"] = nullptr;
    }
    message_history_->AddTextEntry(
        peer_id,
        diff_json.dump(),
        messaging::MessageDirection::received,
        messaging::HistoryEntryKind::systemMessage
    );
  }

  contact_manager_->UpdateContactProfile(peer_id, display_name, bio, photo);

  if (on_contact_profile_updated_) {
    (*on_contact_profile_updated_)(peer_id);
  }
}

auto Client::ParseDeviceProfile(Json const& jprofile) -> Device::Profile {
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

}  // namespace eillance
