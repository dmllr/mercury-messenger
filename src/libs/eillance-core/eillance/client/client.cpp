#include "eillance/client/client.hpp"

#include <stdexcept>
#include <variant>

#include "eillance/rtc/stream_source.hpp"

namespace eillance {

Client::Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport)
    : storage_(std::move(storage)), transport_(std::move(transport)) {
  transport_->setDelegate(this);

  if (auto db_path = storage_->GetPath(); !db_path.empty()) {
    backup_manager_ = std::make_unique<backup::BackupManager>(db_path);
  }

  id_manager_ = std::make_unique<identity::IdManager>(storage_);
  settings_manager_ = std::make_unique<SettingsManager>(storage_);
  contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
  message_history_ = std::make_unique<messaging::MessageHistory>(storage_);

  // Register the status changed callback here to ensure it's always active.
  message_history_->SetStatusChangedCallback([this](const ContactID& p_id, std::uint64_t msg_id, messaging::MessageStatus status) {
    if (on_message_status_changed_) { (*on_message_status_changed_)(p_id, msg_id, status); }
  });

  delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(storage_, contact_manager_, [this](const ContactID& peer_id) {
    if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
      SendQueuedMessages(peer_id);
    } else if (!peer) {
      Connect(peer_id);
    }
  });

  transport_->SetStatusCallback([this](const ContactID& peer_id, const EventSource source, int state_val) {
    if (source == EventSource::peerConnectionState) {
      const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      if (state == eillance::transport::PeerConnectionState::Failed || state == eillance::transport::PeerConnectionState::Closed || state == eillance::transport::PeerConnectionState::Disconnected) {
        // The peer connection is no longer viable.
        // Explicitly close the underlying connection to stop any pending events (like late data messages)
        // from trying to access the Peer object we are about to remove.
        transport_->closePeerConnection(peer_id, "data");
        // Also clean up media connection if it exists, to be safe.
        transport_->closePeerConnection(peer_id, "media");

        // Removing the peer object from the transport layer
        // ensures that a new connection attempt by the MessageDeliveryService will start from a clean state.
        transport_->removePeer(peer_id);
        delivery_service_->connectionAttemptFinished(peer_id);
      }
    }
    if (status_callback_) { (*status_callback_)(peer_id, source, state_val); }
  });
}

Client::~Client() = default;

void Client::InitializeAVStream() {
  if (!video_capture_ || !audio_capture_) { return; }
  av_stream_ = std::make_shared<Stream>(std::make_shared<StreamSource>(std::move(*video_capture_)), std::make_shared<StreamSource>(std::move(*audio_capture_)));
  av_stream_->OnSample([this](Stream::Source::Type type, uint64_t sample_time, const std::vector<std::byte>& sample) { SendSampleToPeers(sample, type, sample_time); });
}

auto Client::GetId() const -> ContactID {
  return ContactID(id_manager_->GetId());
}

auto Client::IdentityExists() const -> bool {
  return id_manager_->IdentityExists();
}

auto Client::GenerateNewIdentity() -> std::string {
  return id_manager_->GenerateNewIdentity();
}

auto Client::GetMnemonic() -> std::optional<std::string> {
  return id_manager_->GetMnemonic();
}

auto Client::RestoreIdentity(const std::string& mnemonic) -> bool {
  return id_manager_->RestoreFromMnemonic(mnemonic);
}

auto Client::CreateBackup(const std::string& output_path, const std::string& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->CreateBackup(output_path, password);
}

auto Client::RestoreBackup(const std::string& input_path, const std::string& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->RestoreFromBackup(input_path, password);
}

auto Client::GetStorage() const -> std::shared_ptr<storage::PersistentStorage> {
  return storage_;
}

auto Client::GetSettingsManager() const -> SettingsManager* {
  return settings_manager_.get();
}

void Client::SetName(const std::string& name) const {
  transport_->SetName(name);
}

void Client::Start() {
  if (!id_manager_->IdentityExists()) {
    LOGE << "Client started without an identity. Please generate or restore one.";
    return;
  }
  if (is_running_.exchange(true)) {
    return;
  }

  if (auto url = settings_manager_->GetSignalingServerUrl()) {
    transport_->setSignalingUrl(*url);
  }

  transport_->setId(GetId());
  transport_->Start();
  inactivity_check_queue_.Dispatch([this] { CheckForInactivePeers(); });
}

void Client::Terminate() {
  is_running_ = false;
  delivery_service_->stop();
  transport_->Terminate();
}

void Client::SetStatusCallback(const StatusCallbackFunction& callback) {
  status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
}

void Client::SetFrameReceivedCallback(const OnFrameReceivedCallback& callback) {
  on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback);
}

void Client::SetProfileReceivedCallback(const OnProfileReceivedCallback& callback) {
  on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback);
}

void Client::SetChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback) {
  on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback);
}

void Client::SetContactRequestCallback(const OnContactRequestCallback& callback) {
  on_contact_request_ = std::make_shared<OnContactRequestCallback>(callback);
}

void Client::SetContactStatusChangedCallback(const OnContactStatusChangedCallback& callback) {
  on_contact_status_changed_ = std::make_shared<OnContactStatusChangedCallback>(callback);
}

void Client::SetMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback) {
  on_message_status_changed_ = std::make_shared<OnMessageStatusChangedCallback>(callback);
}

void Client::SetPeerListUpdateCallback(const OnPeerListUpdateCallback& callback) {
  on_peer_list_update_ = std::make_shared<OnPeerListUpdateCallback>(callback);
}

void Client::SetIncomingCallCallback(const OnIncomingCallCallback& callback) {
  on_incoming_call_ = std::make_shared<OnIncomingCallCallback>(callback);
}

void Client::SetOnCallRingingCallback(const OnCallRingingCallback& callback) {
  on_call_ringing_ = std::make_shared<OnCallRingingCallback>(callback);
}

void Client::SetOnCallAcceptedCallback(const OnCallAcceptedCallback& callback) {
  on_call_accepted_ = std::make_shared<OnCallAcceptedCallback>(callback);
}

void Client::SetOnCallTerminatedCallback(const OnCallTerminatedCallback& callback) {
  on_call_terminated_ = std::make_shared<OnCallTerminatedCallback>(callback);
}

void Client::SetOnMyProfileUpdatedCallback(const OnMyProfileUpdatedCallback& callback) {
  on_my_profile_updated_ = std::make_shared<OnMyProfileUpdatedCallback>(callback);
}

void Client::SetOnContactRemovedCallback(const OnContactRemovedCallback& callback) {
  on_contact_removed_ = std::make_shared<OnContactRemovedCallback>(callback);
}

void Client::SetOnContactProfileUpdatedCallback(const OnContactProfileUpdatedCallback& callback) {
  on_contact_profile_updated_ = std::make_shared<OnContactProfileUpdatedCallback>(callback);
}

void Client::Call(const ContactID& peer_id) {
  Connect(peer_id);
}

void Client::Connect(const ContactID& peer_id) {
  if (transport_->getPeer(peer_id)) {
    LOGW << "Connect called for peer that already exists: " << peer_id;
    return;
  }
  const auto peer = std::make_shared<Peer>(peer_id);
  transport_->addPeer(peer_id, peer);
  SetupDataChannel(peer, peer_id);
  transport_->createDataPeerConnection(peer_id);
  transport_->createDataChannel(peer_id);
  transport_->createOffer(peer_id, "data");
}

void Client::StartVoiceCall(const ContactID& peer_id) {
  StartMediaCall(peer_id, false);
}

void Client::StartVideoCall(const ContactID& peer_id) {
  StartMediaCall(peer_id, true);
}

void Client::AcceptCall(const ContactID& peer_id, messaging::HistoryEntryKind call_kind) const {
  auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    LOGW << "Cannot accept call from " << peer_id << ", peer object not found.";
    return;
  }

  const Json msg = {{"action", "call_accept"}, {"kind", call_kind}};
  message_history_->AddTextEntry(peer_id, msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
  delivery_service_->attemptDelivery(peer_id);

  if (on_call_accepted_) { (*on_call_accepted_)(peer_id); }
}

void Client::RejectCall(const ContactID& peer_id) const {
  Hangup(peer_id);
}

void Client::AddContact(const ContactID& peer_id, const std::optional<std::string>& token) const {
  const auto existing_contact = contact_manager_->GetContact(peer_id);
  if (existing_contact && existing_contact->status == identity::ContactStatus::awaitingApproval) {
    ConfirmContact(peer_id);
    return;
  }
  if (contact_manager_->AddContact(peer_id)) {
    Json content = {
        {"action", "contact_request"},
        {"profile", {{"display_name", settings_manager_->GetMyDisplayName().value_or("")}, {"bio", settings_manager_->GetMyBio().value_or("")}, {"photo", settings_manager_->GetMyPhotoBase64()}}},
    };
    if (token) {
      content["token"] = *token;
    }
    message_history_->AddTextEntry(peer_id, content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
    if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::pendingApproval); }
    delivery_service_->attemptDelivery(peer_id);
  }
}

auto Client::GenerateContactToken() -> std::string {
  return contact_manager_->GenerateContactToken();
}

void Client::DeleteContact(const ContactID& peer_id) const {
  if (auto peer = transport_->getPeer(peer_id)) {
    transport_->closePeerConnection(peer_id, "data");
    transport_->closePeerConnection(peer_id, "media");
    transport_->removePeer(peer_id);
  }
  contact_manager_->DeleteContact(peer_id);
}

void Client::BlockContact(const ContactID& peer_id, bool notify_peer) const {
  if (notify_peer) {
    if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_) {
      const Json msg = {{"type", "contact_removed"}};
      transport_->sendPeerDataChannelMessage(peer_id, msg);
    }
  }

  if (auto peer = transport_->getPeer(peer_id)) {
    transport_->closePeerConnection(peer_id, "data");
    transport_->closePeerConnection(peer_id, "media");
    transport_->removePeer(peer_id);
  }
  contact_manager_->BlockContact(peer_id);
}

void Client::UnblockContact(const ContactID& peer_id) {
  if (contact_manager_->UnblockContact(peer_id)) {
    if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::awaitingApproval); }
  }
}

void Client::ConfirmContact(const ContactID& peer_id) const {
  if (contact_manager_->ConfirmContact(peer_id)) {
    const Json content = {{"action", "contact_confirm"}};
    message_history_->AddTextEntry(peer_id, content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    auto signature_bytes = id_manager_->Sign(peer_id.ToString());
    auto signature_hex = crypto::ToHex(signature_bytes);
    contact_manager_->StoreMySignatureFor(peer_id, signature_hex);

    const Json sig_content = {{"action", "contact_signature"}, {"signature", signature_hex}};
    message_history_->AddTextEntry(peer_id, sig_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    const Json profile_content = {{"action", "send_profile"}};
    message_history_->AddTextEntry(peer_id, profile_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed); }
    delivery_service_->attemptDelivery(peer_id);
  }
}

auto Client::GetPeerConnectionState(const ContactID& peer_id) const -> eillance::transport::PeerConnectionState {
  return transport_->getPeerConnectionState(peer_id);
}

auto Client::GetContacts() const -> std::vector<identity::Contact> {
  return contact_manager_->GetContacts();
}

auto Client::GetChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary> {
  return message_history_->GetChatSummaries(peer_ids);
}

void Client::Hangup(const ContactID& peer_id, bool notify_peer) const {
  if (const auto peer = transport_->getPeer(peer_id); peer != nullptr) {
    peer->pending_offer_sdp.reset();
    if (!peer->pending_candidates.empty()) { peer->pending_candidates.clear(); }

    if (peer->has_media_peer_connection_) {
      LOGI << "Closing media peer connection for " << peer_id;
      transport_->closePeerConnection(peer_id, "media");
      peer->has_media_peer_connection_ = false;
      peer->audio_ = nullptr;
      peer->video_ = nullptr;
      if (av_stream_) { av_stream_->Stop(); }
    }

    if (notify_peer && peer->data_channel_is_open_) {
      LOGI << "Sending hangup signal to " << peer_id << " via data channel.";
      const Json msg = {{"type", "hangup"}};
      transport_->sendPeerDataChannelMessage(peer_id, msg);
    }
  }
  // Notify listeners (including other ViewModels/Services) that the call is terminated.
  if (on_call_terminated_) { (*on_call_terminated_)(peer_id); }
}

void Client::Configure(const Config& config) const {
  if (av_stream_) {
    av_stream_->Video()->Configure(config);
    av_stream_->Audio()->Configure(config);
  }
}

auto Client::SendChatMessage(const ContactID& peer_id, const std::string& message) -> messaging::HistoryEntryRecord {
  if (!contact_manager_->GetContact(peer_id)) {
    LOGE << "Cannot send message to " << peer_id << ". Not a contact.";
    return {};
  }
  auto entry = message_history_->AddTextEntry(peer_id, message, messaging::MessageDirection::sent);

  if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->attemptDelivery(peer_id);
  }

  return entry;
}

void Client::UpdateSignalingUrl(const std::string& url) const {
  if (is_running_) {
    settings_manager_->SetSignalingServerUrl(url);
    transport_->setSignalingUrl(url);
    transport_->reconnect();
  } else {
    LOGW << "UpdateSignalingUrl called while client is not running. URL will be applied on next start.";
    settings_manager_->SetSignalingServerUrl(url);
  }
}

void Client::MarkMessagesAsRead(const ContactID& peer_id, const std::vector<std::uint64_t>& message_ids) const {
  const auto peer = transport_->getPeer(peer_id);
  message_history_->MarkMessagesAsRead(peer_id, message_ids);
  if (peer && peer->data_channel_is_open_) {
    const Json msg = {
        {"type", "message_read"},
        {"message_ids", message_ids},
    };
    transport_->sendPeerDataChannelMessage(peer_id, msg);
  }
}

auto Client::GetMessageHistoryPage(const ContactID& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage {
  return message_history_->GetHistoryPage(peer_id, page_index, page_size);
}

auto Client::GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string> {
  return message_history_->GetFullMessageContent(message_id);
}

void Client::UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
  if (name) { settings_manager_->SetMyDisplayName(*name); }
  if (bio) { settings_manager_->SetMyBio(*bio); }
  if (photo) { settings_manager_->SetMyPhotoBase64(*photo); }

  if (on_my_profile_updated_) {
    (*on_my_profile_updated_)();
  }

  for (const auto& contact: contact_manager_->GetContacts()) {
    if (contact.status == identity::ContactStatus::confirmed) {
      Json profile_update_msg;
      profile_update_msg["action"] = "profile_update";
      profile_update_msg["display_name"] = settings_manager_->GetMyDisplayName().value_or("");
      profile_update_msg["bio"] = settings_manager_->GetMyBio().value_or("");
      profile_update_msg["photo"] = settings_manager_->GetMyPhotoBase64();

      ContactID id(contact.peer_id);
      message_history_->AddTextEntry(id, profile_update_msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

      if (auto peer = transport_->getPeer(id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
        SendQueuedMessages(id);
      } else {
        delivery_service_->attemptDelivery(id);
      }
    }
  }
}

void Client::SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& override_name) const {
  contact_manager_->SetDisplayNameOverride(peer_id, override_name);
  if (on_contact_profile_updated_) { (*on_contact_profile_updated_)(peer_id); }
}

void Client::ResetContactDisplayNameOverride(const ContactID& peer_id) const {
  contact_manager_->ResetDisplayNameOverride(peer_id);
  if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_) {
    const Json request = {{"type", "profile_request"}};
    transport_->sendPeerDataChannelMessage(peer_id, request);
  }
  if (on_contact_profile_updated_) { (*on_contact_profile_updated_)(peer_id); }
}

void Client::DebugEraseAllHistory() const {
  storage_->DebugClearAllHistory();
}

void Client::DebugEraseAllContacts() const {
  storage_->DebugClearAllContacts();
  storage_->DebugClearAllHistory();
}

void Client::DebugRevokeAllApprovals() const {
  storage_->DebugRevokeAllContactApprovals();
}

void Client::OnPeerReady(const std::shared_ptr<Peer>&) {}

auto Client::Sign(const std::string& message) -> std::vector<std::byte> {
  return id_manager_->Sign(message);
}

void Client::OnLoginSuccess() {
  delivery_service_->Start();
}

void Client::OnSystemMessage(const std::string& type, const Json& message) {
  if (type == "auth_challenge") {
    const auto challenge = message.value("challenge", "");
    if (!challenge.empty()) {
      const auto signature_bytes = Sign(challenge);
      const auto signature_hex = crypto::ToHex(signature_bytes);
      const Json response = {{"type", "auth_response"}, {"id", GetId()}, {"signature", signature_hex}};
      transport_->sendSignalingMessage(response);
    }
  } else if (type == "login_success") {
    OnLoginSuccess();
    if (status_callback_) {
      (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
    }
  } else {
    LOGW << "Received unknown system message of type: " << type;
  }
}

void Client::OnSignalingErrorForPeer(const ContactID& peer_id, const std::string& error) {
  if (error == "peer_not_found") {
    LOGW << "Connection attempt failed for peer " << peer_id << " because they are not online. Will retry when they appear.";
    delivery_service_->connectionAttemptFinished(peer_id);
    if (transport_->getPeer(peer_id)) { transport_->closePeerConnection(peer_id, "data"); }
  }
}

void Client::OnDataChannelOpen(const ContactID& peer_id) {
  auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    LOGW << "onDataChannelOpen called for non-existent peer: " << peer_id;
    return;
  }
  UpdatePeerActivity(peer_id);
  LOGI << "Data channel with " << peer_id << " opened.";

  peer->session_cipher_->Reset();
  peer->data_channel_is_open_ = true;

  if (GetId() > peer_id) {
    auto my_pub_key = peer->session_cipher_->GetPublicKey();
    const Json key_exchange_message = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", false}};
    transport_->sendPeerDataChannelMessage(peer_id, key_exchange_message);
  }
}

void Client::OnFrameReceived(const ContactID& /*peer_id*/, const std::vector<std::byte>& frame) {
  if (on_frame_received_) { (*on_frame_received_)(frame); }
}

void Client::StartMediaCall(const ContactID& peer_id, const bool is_video) {
  const auto call_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

  const Json msg = {{"action", "call_request"}, {"kind", call_kind}};
  message_history_->AddTextEntry(peer_id, msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

  auto peer = transport_->getPeer(peer_id);
  if (peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->attemptDelivery(peer_id);
  }
}

void Client::UpdateAndNotifyPeerList() {
  if (on_peer_list_update_) {
    std::vector<Device> peer_list;
    {
      const std::lock_guard<std::mutex> lock(online_peers_mutex_);
      for (const auto& pair: online_peers_) {
        peer_list.push_back(pair.second);
      }
    }
    (*on_peer_list_update_)(peer_list);
  }
}

void Client::SendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const {
  for (const auto& [id, peer]: transport_->getPeers()) {
    if (peer->GetState() == Peer::State::ready) {
      auto track = (type == Stream::Source::Type::video) ? peer->video_ : peer->audio_;
      if (track) { track->SendSample(sample, sample_time); }
    }
  }
}

void Client::ProcessDataChannelMessage(const ContactID& peer_id, const Json& message) {
  UpdatePeerActivity(peer_id);
  const auto itr = message.find("type");
  if (itr == message.end()) {
    return;
  }
  const auto type = itr->get<std::string>();
  const auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    return;
  };

  if (type == "key_exchange") {
    auto pub_key_bytes = message.at("key").get<std::vector<std::byte>>();
    if (peer->session_cipher_->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
      LOGI << "Successfully computed shared secret with peer " << peer_id;
      const bool is_reply = message.value("is_reply", false);
      if (!is_reply) {
        auto my_pub_key = peer->session_cipher_->GetPublicKey();
        const Json reply = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", true}};
        transport_->sendPeerDataChannelMessage(peer_id, reply);
      } else {
        SendQueuedMessages(peer_id);
      }
    } else {
      LOGE << "Failed to compute shared secret with peer " << peer_id;
    }
    return;
  }

  if (type == "hangup") {
    LOGI << "Received hangup signal from " << peer_id << " via data channel.";
    Hangup(peer_id, false);
    return;
  }

  if (type == "reconfirm_request") {
    const auto contact_info = contact_manager_->GetContact(peer_id);
    if (contact_info && contact_info->my_signature_for_them) {
      LOGI << "Received reconfirm_request from " << peer_id << ". Sending back my signature for them.";
      const Json reconfirm_response = {{"type", "reconfirm_response"}, {"signature", *contact_info->my_signature_for_them}};
      transport_->sendPeerDataChannelMessage(peer_id, reconfirm_response);
    } else {
      LOGW << "Received reconfirm_request from " << peer_id << " but I have no signature for them to send back.";
    }
    return;
  }

  if (type == "reconfirm_response") {
    const auto signature_hex = message.at("signature").get<std::string>();
    const auto signature_bytes = crypto::FromHex(signature_hex);

    if (crypto::Identity::Verify(peer_id.ToString(), GetId().ToString(), signature_bytes)) {
      LOGI << "Re-confirmation from " << peer_id << " successful. Re-adding as contact.";
      contact_manager_->MarkAsConfirmed(peer_id);

      const auto my_signature_bytes = id_manager_->Sign(peer_id.ToString());
      const auto my_signature_hex = crypto::ToHex(my_signature_bytes);
      contact_manager_->StoreMySignatureFor(peer_id, my_signature_hex);

      const Json sig_content = {{"type", "contact_signature"}, {"signature", my_signature_hex}};
      transport_->sendPeerDataChannelMessage(peer_id, sig_content);

      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
      }

      const Json request_profile = {{"type", "profile_request"}};
      transport_->sendPeerDataChannelMessage(peer_id, request_profile);
      const Json send_profile = {{"action", "send_profile"}};
      message_history_->AddTextEntry(peer_id, send_profile.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
      delivery_service_->attemptDelivery(peer_id);
    } else {
      LOGW << "Re-confirmation from " << peer_id << " failed. Signature invalid.";
    }
    return;
  }

  if (type == "contact_signature") {
    auto signature_hex = message.at("signature").get<std::string>();
    auto signature_bytes = crypto::FromHex(signature_hex);
    if (crypto::Identity::Verify(peer_id.ToString(), GetId().ToString(), signature_bytes)) {
      LOGI << "Received and verified contact signature from " << peer_id;
      contact_manager_->StoreSignatureFrom(peer_id, signature_hex);
    } else {
      LOGW << "Received invalid contact signature from " << peer_id;
    }
    return;
  }

  if (!contact_manager_->IsConfirmedContact(peer_id)) {
    LOGW << "Received data channel message of type '" << type << "' from non-contact peer " << peer_id << ". Requesting re-confirmation.";
    const Json reconfirm_request = {{"type", "reconfirm_request"}};
    transport_->sendPeerDataChannelMessage(peer_id, reconfirm_request);
    return;
  }

  if (type == "profile") {
    const auto profile = ParseDeviceProfile(message.at("profile"));

    {
      const std::lock_guard<std::mutex> lock(online_peers_mutex_);
      online_peers_[peer_id].id = peer_id.ToString();
      online_peers_[peer_id].profile = profile;
      if (message.contains("name")) { online_peers_[peer_id].name = message.at("name").get<std::string>(); }
    }
    UpdateAndNotifyPeerList();
    if (on_profile_received_) { (*on_profile_received_)(peer_id, profile); }
  } else if (type == "termination") {
    transport_->removePeer(peer_id);
  } else if (type == "chat") {
    if (!peer->session_cipher_->IsReady()) {
      LOGW << "Received chat message from " << peer_id << " but session cipher is not ready. Ignoring.";
      return;
    }
    auto message_id = message.at("message_id").get<std::uint64_t>();
    bool is_system = message.value("is_system", false);
    bool message_processed_successfully = false;

    if (is_system) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        message_processed_successfully = true;
        const auto content_json = Json::parse(decrypted_message);
        const auto action = content_json.value("action", "");
        if (action == "contact_signature") {
          auto signature_hex = content_json.at("signature").get<std::string>();
          auto signature_bytes = crypto::FromHex(signature_hex);
          if (crypto::Identity::Verify(peer_id.ToString(), GetId().ToString(), signature_bytes)) {
            LOGI << "Received and verified contact signature from " << peer_id;
            contact_manager_->StoreSignatureFrom(peer_id, signature_hex);
            auto my_contact_info_for_them = contact_manager_->GetContact(peer_id);
            if (!my_contact_info_for_them || !my_contact_info_for_them->my_signature_for_them) {
              auto my_signature_bytes = id_manager_->Sign(peer_id.ToString());
              auto my_signature_hex = crypto::ToHex(my_signature_bytes);
              contact_manager_->StoreMySignatureFor(peer_id, my_signature_hex);
              const Json sig_content = {{"action", "contact_signature"}, {"signature", my_signature_hex}};
              message_history_->AddTextEntry(peer_id, sig_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
              delivery_service_->attemptDelivery(peer_id);
            }
          } else {
            LOGW << "Received invalid contact signature from " << peer_id;
          }
        } else if (action == "call_request") {
          if (on_incoming_call_) {
            const messaging::HistoryEntryKind call_type = content_json.value("kind", messaging::HistoryEntryKind::voiceCall);
            (*on_incoming_call_)(peer_id, call_type);

            const Json ringing_msg = {{"action", "call_ringing"}};
            message_history_->AddTextEntry(peer_id, ringing_msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
            delivery_service_->attemptDelivery(peer_id);
          }
        } else if (action == "call_ringing") {
          if (on_call_ringing_) {
            (*on_call_ringing_)(peer_id);
          }
        } else if (action == "call_accept") {
          if (on_call_accepted_) {
            (*on_call_accepted_)(peer_id);
          }
          const auto contact = contact_manager_->GetContact(peer_id);
          if (contact && contact->status == identity::ContactStatus::confirmed) {
            const bool is_video = content_json.value("kind", messaging::HistoryEntryKind::voiceCall) == messaging::HistoryEntryKind::videoCall;
            if (av_stream_) { av_stream_->Start(); }
            auto accepted_peer = transport_->getPeer(peer_id);
            if (!accepted_peer) return;
            transport_->createMediaPeerConnection(peer_id);
            accepted_peer->has_media_peer_connection_ = true;
            accepted_peer->audio_ = transport_->addAudio(peer_id, [this, wpeer = make_weak_ptr(accepted_peer)]() {
              if (const auto p = wpeer.lock()) { transport_->addToStream(p, false); }
            });
            if (is_video) {
              accepted_peer->video_ = transport_->addVideo(peer_id, [this, wpeer = make_weak_ptr(accepted_peer)]() {
                if (const auto p = wpeer.lock()) { transport_->addToStream(p, true); }
              });
            }
            transport_->createOffer(peer_id, "media");
          }
        } else if (action == "profile_update") {
          HandleProfileUpdate(peer_id, content_json);
        }
      } else {
        LOGW << "Failed to decrypt system message from " << peer_id;
      }
    } else if (on_chat_message_received_) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        message_processed_successfully = true;
        auto history_entry = message_history_->AddTextEntry(peer_id, decrypted_message, messaging::MessageDirection::received);
        (*on_chat_message_received_)(peer_id, history_entry);
      } else {
        LOGW << "Failed to decrypt message from " << peer_id;
      }
    }

    if (message_processed_successfully) {
      const Json receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      transport_->sendPeerDataChannelMessage(peer_id, receipt);
      SendQueuedMessages(peer_id);
    } else {
      LOGW << "Skipping receipt for message " << message_id << " from " << peer_id << " due to processing failure.";
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
    const auto message_ids = message.at("message_ids").get<std::vector<std::uint64_t>>();
    for (const auto msg_id: message_ids) {
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
    transport_->sendPeerDataChannelMessage(peer_id, profile_msg);
  } else if (type == "contact_removed") {
    LOGI << "Received contact removal notification from " << peer_id;
    if (on_contact_removed_) {
      (*on_contact_removed_)(peer_id);
    }
    BlockContact(peer_id, false);
  } else {
    throw std::runtime_error("Unknown message type in ProcessDataChannelMessage");
  }
}

void Client::SendQueuedMessages(const ContactID& peer_id) {
  const auto messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  if (messages.empty()) {
    delivery_service_->connectionAttemptFinished(peer_id);
    return;
  }
  const auto peer = transport_->getPeer(peer_id);
  if (!peer) { return; }
  LOGI << "Processing " << messages.size() << " queued message(s) for " << peer_id;
  for (const auto& entry: messages) {
    bool sent = false;
    switch (entry.kind) {
      case messaging::HistoryEntryKind::textMessage:
        [[fallthrough]];
      case messaging::HistoryEntryKind::systemMessage: {
        if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          LOGW << "Skipping message " << entry.id << " with unexpected data type for its kind.";
          UpdatePeerActivity(peer_id);
          continue;
        }

        std::string content_to_send;
        const auto& msg_entry = std::get<messaging::MessageEntry>(entry.data);
        if (entry.is_truncated) {
          auto full_content = message_history_->GetFullMessageContent(entry.id);
          if (full_content) {
            content_to_send = *full_content;
          } else {
            LOGE << "Failed to retrieve full content for truncated message " << entry.id
                 << ", skipping.";
            continue;
          }
        } else {
          content_to_send = msg_entry.content;
        }

        if (entry.kind == messaging::HistoryEntryKind::textMessage) {
          if (peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
            auto encrypted_payload = peer->session_cipher_->Encrypt(content_to_send);
            UpdatePeerActivity(peer_id);
            if (encrypted_payload.empty()) {
              LOGE << "Failed to encrypt chat message for peer " << peer_id;
              continue;
            }
            const Json json_message = {{"type", "chat"},
                                       {"message_id", entry.id},
                                       {"is_system", false},
                                       {"payload", std::vector<std::byte>(encrypted_payload.begin(),
                                                                          encrypted_payload.end())}};
            transport_->sendPeerDataChannelMessage(peer_id, json_message);
            sent = true;
          }
        } else {  // systemMessage
          try {
            Json content_json = Json::parse(content_to_send);
            const std::string action = content_json.value("action", "");
            if (action == "contact_request" || action == "contact_confirm") {
              Json msg = {{"type", action == "contact_request" ? "contact_add_request" : "contact_confirm"}, {"to", peer_id}};
              if (action == "contact_request") {
                if (content_json.contains("profile")) { msg["profile"] = content_json["profile"]; }
                if (content_json.contains("token")) {
                  msg["token"] = content_json["token"];
                }
              }
              transport_->sendSignalingMessage(msg);
              sent = true;
            } else if (action == "profile_update" || action == "contact_signature" || action == "call_accept" || action == "call_request" || action == "call_ringing") {
              if (peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
                auto encrypted_payload = peer->session_cipher_->Encrypt(content_to_send);
                UpdatePeerActivity(peer_id);
                if (encrypted_payload.empty()) {
                  LOGE << "Failed to encrypt system message (" << action << ") for peer " << peer_id;
                  continue;
                }
                const Json json_message = {{"type", "chat"},
                                           {"message_id", entry.id},
                                           {"is_system", true},
                                           {"payload", std::vector<std::byte>(encrypted_payload.begin(),
                                                                              encrypted_payload.end())}};
                transport_->sendPeerDataChannelMessage(peer_id, json_message);
                sent = true;
              }
            } else if (action == "request_profile") {
              if (peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
                UpdatePeerActivity(peer_id);
                const Json request = {{"type", "profile_request"}};
                transport_->sendPeerDataChannelMessage(peer_id, request);
                sent = true;
              }
            } else if (action == "send_profile") {
              if (peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
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
                transport_->sendPeerDataChannelMessage(peer_id, profile_msg);
                sent = true;
              }
            }
          } catch (const Json::parse_error& e) {
            LOGE << "Failed to parse system message content for id " << entry.id << ": " << e.what();
          }
        }
        break;
      }
      default:
        LOGW << "Don't know how to deliver queued message of kind: " << static_cast<int>(entry.kind);
        break;
    }
    if (sent) {
      message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
    }
  }
  delivery_service_->connectionAttemptFinished(peer_id);
}

void Client::ProcessPeerMessage(Json message) {
  const auto from_it = message.find("from");
  if (from_it == message.end()) { return; }
  ContactID peer_id(from_it->get<std::string>());
  const auto type_it = message.find("type");
  if (type_it == message.end()) { return; }
  auto type = type_it->get<std::string>();
  const auto context = message.value("context", "data");

  if (context == "media") {
    auto peer = transport_->getPeer(peer_id);
    if (!peer) {
      LOGI << "Received media message for unknown peer " << peer_id << ". Creating peer object first.";
      peer = std::make_shared<Peer>(peer_id);
      transport_->addPeer(peer_id, peer);
    }
    if (type == "offer") {
      LOGD << "Received media offer from " << peer_id << ". Replying with answer.";
      const auto sdp = message["sdp"].get<std::string>();
      const bool is_video_call = sdp.find("m=video") != std::string::npos;
      if (av_stream_) { av_stream_->Start(); }
      if (!peer->has_media_peer_connection_) {
        transport_->createMediaPeerConnection(peer_id);
        peer->has_media_peer_connection_ = true;
      }
      peer->audio_ = transport_->addAudio(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
        if (const auto p = wpeer.lock()) { transport_->addToStream(p, false); }
      });
      if (is_video_call) {
        peer->video_ = transport_->addVideo(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
          if (const auto p = wpeer.lock()) { transport_->addToStream(p, true); }
        });
      }
      transport_->setRemoteDescription(peer_id, "media", sdp, "offer");
      transport_->createAnswer(peer_id, "media");
    } else if (type == "candidate") {
      const auto candidate_str = message["candidate"].get<std::string>();
      const auto mid = message["sdpMid"].get<std::string>();
      transport_->addRemoteCandidate(peer_id, "media", candidate_str, mid);
    } else if (peer->has_media_peer_connection_ && type == "answer") {
      const auto sdp = message["sdp"].get<std::string>();
      LOGD << "Remote SDP (" << type << ") for " << peer_id << " context " << context << ":\n"
           << sdp;
      transport_->setRemoteDescription(peer_id, "media", sdp, type);
    } else {
      LOGW << "Received media message of type '" << type << "' for " << peer_id << " in an unexpected state.";
    }
    return;
  }

  // Handle data channel signaling
  if (type == "offer") {
    auto peer = transport_->getPeer(peer_id);
    if (peer) {                 // Glare condition
      if (GetId() > peer_id) {  // This peer is "polite"
        LOGW << "Glare detected, polite peer yielding. Resetting connection to handle incoming offer from " << peer_id;
        transport_->closePeerConnection(peer_id, "data");
        transport_->removePeer(peer_id);
        peer = nullptr;
      } else {  // This peer is "impolite"
        LOGW << "Glare detected, and we are the 'impolite' peer. Ignoring incoming offer from " << peer_id;
        return;  // Let our outgoing offer attempt proceed.
      }
    }

    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      transport_->addPeer(peer_id, peer);
      SetupDataChannel(peer, peer_id);
      transport_->createDataPeerConnection(peer_id);
      const auto sdp = message["sdp"].get<std::string>();
      transport_->setRemoteDescription(peer_id, "data", sdp, "offer");
      transport_->createAnswer(peer_id, "data");
    }
  } else if (type == "answer") {
    const auto sdp = message["sdp"].get<std::string>();
    transport_->setRemoteDescription(peer_id, "data", sdp, "answer");
  } else if (type == "candidate") {
    const auto candidate = message["candidate"].get<std::string>();
    const auto mid = message["sdpMid"].get<std::string>();
    transport_->addRemoteCandidate(peer_id, "data", candidate, mid);
  } else if (type == "contact_add_request") {
    std::optional<std::string> token;
    if (message.contains("token") && message.at("token").is_string()) {
      token = message.at("token").get<std::string>();
    }

    std::optional<std::string> name;
    std::optional<std::string> bio;
    std::optional<std::string> photo_base64;
    if (message.contains("profile")) {
      const auto& profile = message["profile"];
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

    if (result == identity::RequestStatus::New || result == identity::RequestStatus::Duplicate) {
      if (token && contact_manager_->ValidateAndConsumeContactToken(*token)) {
        LOGI << "Received contact request from " << peer_id << " with a valid token. Auto-confirming.";
        ConfirmContact(peer_id);
        return;
      }

      const Json content = {{"action", "contact_request_received"}};
      auto entry = message_history_->AddTextEntry(peer_id, content.dump(), messaging::MessageDirection::received, messaging::HistoryEntryKind::systemMessage);
      message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::read);

      if (result == identity::RequestStatus::New) {
        if (on_contact_request_) { (*on_contact_request_)(peer_id); }
      } else {  // Duplicate
        LOGI << "Received duplicate contact request from " << peer_id << ".";
        if (on_contact_profile_updated_) { (*on_contact_profile_updated_)(peer_id); }
      }
    } else {  // Ignored
      LOGI << "Contact request from " << peer_id << " ignored (contact is confirmed or was rejected).";
      if (contact_manager_->IsConfirmedContact(peer_id)) {
        LOGI << "Resending confirmation to already confirmed peer " << peer_id;
        const Json msg = {{"type", "contact_confirm"}, {"to", peer_id}};
        transport_->sendSignalingMessage(msg);
      }
    }
  } else if (type == "contact_confirm") {
    if (contact_manager_->MarkAsConfirmed(peer_id)) {
      LOGI << "Contact " << peer_id << " has confirmed your request.";
      if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed); }

      auto pending_requests = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sent);
      for (const auto& req: pending_requests) {
        if (req.kind == messaging::HistoryEntryKind::systemMessage) {
          const auto& content_str = std::get<messaging::MessageEntry>(req.data).content;
          try {
            const Json content_json = Json::parse(content_str);
            if (content_json.value("action", "") == "contact_request") {
              message_history_->UpdateMessageStatus(peer_id, req.id, messaging::MessageStatus::read);
              break;
            }
          } catch (...) {
          }
        }
      }

      const Json profile_content = {{"action", "send_profile"}};
      message_history_->AddTextEntry(peer_id, profile_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

      const Json request_content = {{"action", "request_profile"}};
      message_history_->AddTextEntry(peer_id, request_content.dump(), messaging::MessageDirection::sent,
                                     messaging::HistoryEntryKind::systemMessage);

      delivery_service_->attemptDelivery(peer_id);
    }
  }
}

void Client::CheckForInactivePeers() {
  if (!is_running_) { return; }

  auto peers = transport_->getPeers();
  auto now = Time::Now();
  const auto timeout_s = settings_manager_->GetPeerInactivityTimeout();
  const auto timeout_us = std::chrono::duration_cast<std::chrono::microseconds>(timeout_s).count();

  for (const auto& [peer_id, peer]: peers) {
    if (!peer->has_media_peer_connection_ && GetPeerConnectionState(peer_id) == transport::PeerConnectionState::Connected) {
      if ((now - peer->last_activity_time_.load()) > timeout_us) {
        LOGI << "Peer " << peer_id << " inactive for too long. Closing data connection.";
        transport_->closePeerConnection(peer_id, "data");
      }
    }
  }
  inactivity_check_queue_.DispatchAfter(kPeerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
}

void Client::UpdatePeerActivity(const ContactID& peer_id) {
  if (auto peer = transport_->getPeer(peer_id)) {
    peer->last_activity_time_ = Time::Now();
  }
}

void Client::SetupDataChannel(const std::shared_ptr<Peer>& peer, const ContactID& peer_id) {
  peer->on_data_channel_message_ = [this, peer_id](const std::string& msg) {
    ProcessDataChannelMessage(peer_id, Json::parse(msg));
  };
}

void Client::HandleProfileUpdate(const ContactID& peer_id, const Json& profile_json) const {
  auto get_optional_from_json = [](const Json& j, const char* key) -> std::optional<std::string> {
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

  const bool name_changed = display_name && (!old_contact_opt || old_contact_opt->display_name != *display_name);
  const bool bio_changed = bio && (!old_contact_opt || old_contact_opt->bio != *bio);
  const bool photo_changed = photo && (!old_contact_opt || old_contact_opt->photo_base64 != *photo);

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
    message_history_->AddTextEntry(peer_id, diff_json.dump(), messaging::MessageDirection::received,
                                   messaging::HistoryEntryKind::systemMessage);
  }

  contact_manager_->UpdateContactProfile(peer_id, display_name, bio, photo);

  if (on_contact_profile_updated_) {
    (*on_contact_profile_updated_)(peer_id);
  }
}

auto Client::ParseDeviceProfile(const Json& jprofile) -> Device::Profile {
  Device::Profile profile;
  for (const auto& cam_item: jprofile) {
    Device::Camera camera;
    camera.id = cam_item.at("id").get<std::string>();
    camera.name = cam_item.at("name").get<std::string>();
    for (const auto& res_item: cam_item.at("resolutions")) {
      camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
    }
    profile.push_back(camera);
  }
  return profile;
}

}  // namespace eillance
