#include "eillance/client/client.hpp"

#include <chrono>
#include <iostream>

#include <eillance/events/service_connection_state.hpp>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance {

Client::Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport)
    : storage_(std::move(storage)),
      transport_(std::move(transport)) {
  transport_->SetDelegate(this);

  // Initialize Services
  account_service_ = std::make_shared<services::AccountService>(storage_, transport_);

  // Shared ContactManager
  auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

  contact_service_ = std::make_shared<services::ContactService>(
      storage_,
      transport_,
      contact_manager,
      account_service_.get(),
      [this](ContactID const& id) { this->Connect(id); }  // Inject connection logic
  );

  messaging_service_ = std::make_shared<services::MessagingService>(
      storage_,
      transport_,
      contact_manager,
      [this](ContactID const& id) { this->Connect(id); }  // Inject connection logic
  );
  media_call_service_ = std::make_shared<services::MediaCallService>(transport_);

  // Wire dependencies
  contact_service_->SetMessagingService(messaging_service_.get());
  media_call_service_->SetMessagingService(messaging_service_.get());

  // Wire Transport Status
  transport_->SetStatusCallback([this](ContactID const& peer_id, EventSource const source, int state_val) {
    if (source == EventSource::peerConnectionState) {
      auto const state = static_cast<transport::PeerConnectionState>(state_val);
      if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
          state == transport::PeerConnectionState::disconnected) {
        media_call_service_->OnMediaPeerConnectionStateChange(peer_id, state);
        messaging_service_->OnConnectionAttemptFinished(peer_id);

        transport_->ClosePeerConnection(peer_id, "data");
        transport_->ClosePeerConnection(peer_id, "media");
        transport_->RemovePeer(peer_id);
      }
    }

    INVARIANT(status_callback_);
    (*status_callback_)(peer_id, source, state_val);
  });
}

Client::~Client() { Terminate(); }

void Client::Start() {
  if (!account_service_->IdentityExists()) {
    LGE << "Client started without an identity.";
    return;
  }
  if (is_running_.exchange(true)) {
    return;
  }

  if (auto url = account_service_->GetSettings()->GetSignalingServerUrl()) {
    transport_->SetSignalingUrl(*url);
  }

  transport_->SetId(account_service_->GetId());
  transport_->Start();
  messaging_service_->StartDeliveryService();

  background_queue_.Dispatch([this] { CheckForInactivePeers(); });
}

void Client::Terminate() {
  is_running_ = false;
  messaging_service_->StopDeliveryService();
  transport_->Terminate();
}

void Client::Configure(Config const& config) const { media_call_service_->Configure(config); }

void Client::OnPeerReady(std::shared_ptr<transport::Peer> const&) {}

void Client::OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) {
  if (error == "peer_not_found") {
    LGW << "Connection attempt failed for peer " << peer_id.ToString() << " (not online).";
    messaging_service_->OnConnectionAttemptFinished(peer_id);
    if (transport_->GetPeer(peer_id)) {
      transport_->ClosePeerConnection(peer_id, "data");
    }
  }
}

auto Client::Sign(std::span<std::byte const> message) -> std::vector<std::byte> {
  return account_service_->Sign(message);
}

void Client::OnLoginSuccess() { messaging_service_->StartDeliveryService(); }

void Client::OnSystemMessage(std::string const& type, Json const& message) {
  if (type == "auth_challenge") {
    auto const challenge = message.value("challenge", "");
    if (!challenge.empty()) {
      std::span<std::byte const> const challenge_bytes(
          reinterpret_cast<std::byte const*>(challenge.data()),
          challenge.size()
      );
      auto const signature = account_service_->Sign(challenge_bytes);
      Json const response = {
          {"type", "auth_response"},
          {"id", GetId().ToString()},
          {"signature", crypto::ToHex(signature)}
      };
      transport_->SendSignalingMessage(response);
    }
  } else if (type == "login_success") {
    OnLoginSuccess();
    INVARIANT(status_callback_);
    (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
  } else {
    LGW << "Received unknown system message of type: " << type;
  }
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

  // Media Signaling
  if (context == "media") {
    auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      transport_->AddPeer(peer_id, peer);
    }

    if (type == "offer") {
      transport_->CreateMediaPeerConnection(peer_id);
      peer->has_media_peer_connection = true;

      auto const sdp = message["sdp"].get<std::string>();
      bool const is_video = sdp.find("m=video") != std::string::npos;
      media_call_service_->OnCallRequest(
          peer_id,
          is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall
      );
      transport_->SetRemoteDescription(peer_id, "media", sdp, "offer");

    } else if (type == "candidate") {
      auto const candidate_str = message["candidate"].get<std::string>();
      auto const mid = message["sdpMid"].get<std::string>();
      transport_->AddRemoteCandidate(peer_id, "media", candidate_str, mid);
    } else if (type == "answer") {
      auto const sdp = message["sdp"].get<std::string>();
      transport_->SetRemoteDescription(peer_id, "media", sdp, type);
    }
    return;
  }

  // Data Signaling
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
      peer = std::make_shared<transport::Peer>(peer_id);
      transport_->AddPeer(peer_id, peer);
      SetupPeerHandlers(peer, peer_id);
      transport_->CreateDataPeerConnection(peer_id);

      auto const sdp = message["sdp"].get<std::string>();
      transport_->SetRemoteDescription(peer_id, "data", sdp, "offer");
      transport_->CreateAnswer(peer_id, "data");
    }
  } else if (type == "answer") {
    transport_->SetRemoteDescription(peer_id, "data", message["sdp"].get<std::string>(), "answer");
  } else if (type == "candidate") {
    transport_->AddRemoteCandidate(
        peer_id,
        "data",
        message["candidate"].get<std::string>(),
        message["sdpMid"].get<std::string>()
    );
  }

  // Contact & Profile Logic
  else if (type == "contact_add_request") {
    std::optional<std::string> token;
    if (message.contains("token")) {
      token = message["token"];
    }

    Json const profile = message.value("profile", Json::object());
    contact_service_->OnContactAddRequest(peer_id, token, profile);
  } else if (type == "contact_confirm") {
    contact_service_->OnContactConfirm(peer_id);
  }
}

void Client::OnDataChannelOpen(ContactID const& peer_id) {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  }

  LGI << "Data channel with " << peer_id.ToString() << " opened.";

  peer->session_cipher->Reset();
  peer->data_channel_is_open = true;
  peer->last_activity_time = Time::Now();

  if (GetId() > peer_id) {
    auto my_pub_key = peer->session_cipher->GetPublicKey();
    Json const key_msg = {
        {"type", "key_exchange"},
        {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
        {"is_reply", false}
    };
    transport_->SendPeerDataChannelMessage(peer_id, key_msg);
  }

  // Sync profile info with the newly connected peer if they are a confirmed contact
  if (contact_service_->IsConfirmed(peer_id)) {
    // Delegate to contact service to send the profile info via data channel
    contact_service_->SendMyProfile(peer_id);
  }
}

void Client::OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) {
  media_call_service_->OnFrameReceived(peer_id, frame);
}

void Client::ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
  auto const peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  }

  peer->last_activity_time = Time::Now();

  if (!message.contains("type")) {
    return;
  }
  std::string const type = message["type"];

  if (type == "key_exchange") {
    auto pub_key = message["key"].get<std::vector<std::byte>>();
    if (peer->session_cipher->ComputeSharedSecret(pub_key)) {
      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        transport_->SendPeerDataChannelMessage(
            peer_id,
            {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_key.begin(), my_key.end())}, {"is_reply", true}
            }
        );
      }
      messaging_service_->SendQueuedMessages(peer_id);
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
    return;
  }

  // Route to services
  if (type == "chat") {
    auto msg_id = message["message_id"].get<uint64_t>();
    bool const is_sys = message.value("is_system", false);
    auto payload = message["payload"].get<std::vector<std::byte>>();

    std::vector<messaging::Attachment> attachments;
    if (message.contains("attachments")) {
      for (auto const& j_att : message["attachments"]) {
        messaging::Attachment att;
        att.id = j_att.at("id").get<std::string>();
        att.type = j_att.at("type").get<messaging::AttachmentType>();
        att.mime_type = j_att.value("mime", "");
        att.size_bytes = j_att.value("size", 0ULL);
        att.thumbnail_base64 = j_att.value("thumb", "");
        att.is_downloaded = false;  // It's a remote file
        attachments.push_back(att);
      }
    }

    messaging_service_->OnChatMessageReceived(peer_id, payload, msg_id, is_sys, attachments);
  } else if (type == "message_receipt") {
    messaging_service_->OnMessageReceipt(peer_id, message["message_id"], message["status"]);
  } else if (type == "message_read") {
    messaging_service_->OnMessagesRead(peer_id, message["message_ids"].get<std::vector<uint64_t>>());
  } else if (type == "hangup") {
    media_call_service_->Hangup(peer_id, false);
  } else if (type == "contact_removed") {
    contact_service_->OnContactRemoved(peer_id);
  } else if (type == "reconfirm_request") {
    contact_service_->OnReconfirmRequest(peer_id);
  } else if (type == "reconfirm_response") {
    contact_service_->OnReconfirmResponse(peer_id, message["signature"]);
  } else if (type == "contact_signature") {
    contact_service_->OnContactSignature(peer_id, message["signature"]);
  } else if (type == "profile_info") {
    contact_service_->OnProfileUpdate(peer_id, message);
  } else if (type == "profile_request") {
    SendProfileInfo(peer_id);
  } else if (type == "profile") {
    contact_service_->OnDeviceProfileReceived(peer_id, message["profile"]);
  } else if (type == "termination") {
    transport_->RemovePeer(peer_id);
  } else if (type == "file_chunk") {
    messaging_service_->ProcessIncomingFileChunk(peer_id, message);
  }
}

void Client::SetupPeerHandlers(std::shared_ptr<transport::Peer> const& peer, ContactID const& peer_id) {
  peer->on_data_channel_message = [this, peer_id](std::string const& msg) {
    try {
      ProcessDataChannelMessage(peer_id, Json::parse(msg));
    } catch (...) {
      LGE << "Failed to parse data channel message from " << peer_id.ToString();
    }
  };
}

void Client::SendProfileInfo(ContactID const& peer_id) { contact_service_->SendMyProfile(peer_id); }

void Client::BroadcastProfileUpdate() {
  auto const contacts = contact_service_->GetContacts();
  for (auto const& contact : contacts) {
    if (contact.status == identity::ContactStatus::confirmed) {
      auto peer = transport_->GetPeer(contact.peer_id);
      if (peer && peer->data_channel_is_open) {
        SendProfileInfo(contact.peer_id);
      } else {
        // Initiate connection if not currently connected.
        // Once the data channel opens, OnDataChannelOpen will send profile.
        Connect(contact.peer_id);
      }
    }
  }
}

void Client::Connect(ContactID const& peer_id) {
  if (transport_->GetPeer(peer_id)) {
    return;
  }

  auto peer = std::make_shared<transport::Peer>(peer_id);
  transport_->AddPeer(peer_id, peer);
  SetupPeerHandlers(peer, peer_id);
  transport_->CreateDataPeerConnection(peer_id);
  transport_->CreateDataChannel(peer_id);
  transport_->CreateOffer(peer_id, "data");
}

void Client::CheckForInactivePeers() {
  if (!is_running_) {
    return;
  }
  auto const now = Time::Now();
  auto const timeout =
      std::chrono::duration_cast<std::chrono::microseconds>(account_service_->GetSettings()->GetPeerInactivityTimeout())
          .count();

  for (auto const& [id, peer] : transport_->GetPeers()) {
    if (!peer->has_media_peer_connection &&
        transport_->GetPeerConnectionState(id) == transport::PeerConnectionState::connected) {
      if ((now - peer->last_activity_time.load()) > timeout) {
        LGI << "Peer " << id.ToString() << " inactive. Closing.";
        transport_->ClosePeerConnection(id, "data");
      }
    }
  }
  background_queue_.DispatchAfter(kPeerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
}

// --- Facade implementations for backward compatibility ---

auto Client::GetId() const -> ContactID { return account_service_->GetId(); }
auto Client::IdentityExists() const -> bool { return account_service_->IdentityExists(); }
auto Client::GenerateNewIdentity() -> std::string {
  auto mnemonic = account_service_->GenerateNewIdentity();
  BroadcastProfileUpdate();
  return mnemonic;
}
auto Client::GetMnemonic() const -> std::optional<std::string> { return account_service_->GetMnemonic(); }
auto Client::RestoreIdentity(std::string const& m) -> bool {
  if (account_service_->RestoreIdentity(m)) {
    BroadcastProfileUpdate();
    return true;
  }
  return false;
}
auto Client::CreateBackup(std::string const& p, std::string const& pw) const -> bool {
  return account_service_->CreateBackup(p, pw);
}
auto Client::RestoreBackup(std::string const& p, std::string const& pw) -> bool {
  if (account_service_->RestoreBackup(p, pw)) {
    BroadcastProfileUpdate();
    return true;
  }
  return false;
}
void Client::SetName(std::string const& name) const { transport_->SetName(name); }
void Client::UpdateSignalingUrl(std::string const& url) const { account_service_->UpdateSignalingUrl(url); }
void Client::UpdateMyProfile(
    std::optional<std::string> const& n,
    std::optional<std::string> const& b,
    std::optional<std::string> const& p
) {
  account_service_->UpdateMyProfile(n, b, p);
  BroadcastProfileUpdate();
}
auto Client::GetSettingsManager() const -> SettingsManager* { return account_service_->GetSettings(); }

void Client::AddContact(ContactID const& id, std::optional<std::vector<std::byte>> const& t) const {
  contact_service_->AddContact(id, t);
}
auto Client::GenerateContactToken() const -> std::vector<std::byte> { return contact_service_->GenerateContactToken(); }
void Client::ConfirmContact(ContactID const& id) const { contact_service_->ConfirmContact(id); }
void Client::BlockContact(ContactID const& id, bool n) const { contact_service_->BlockContact(id, n); }
void Client::DeleteContact(ContactID const& id) const { contact_service_->DeleteContact(id); }
void Client::UnblockContact(ContactID const& id) const { contact_service_->UnblockContact(id); }
void Client::SetContactDisplayNameOverride(ContactID const& id, std::string const& n) const {
  contact_service_->SetContactDisplayNameOverride(id, n);
}
void Client::ResetContactDisplayNameOverride(ContactID const& id) const {
  contact_service_->ResetContactDisplayNameOverride(id);
}
auto Client::GetContacts() const -> std::vector<identity::Contact> { return contact_service_->GetContacts(); }

auto Client::SendChatMessage(ContactID const& id, std::string const& msg) const -> messaging::HistoryEntryRecord {
  return messaging_service_->SendChatMessage(id, msg);
}
void Client::MarkMessagesAsRead(ContactID const& id, std::vector<std::uint64_t> const& mids) const {
  messaging_service_->MarkMessagesAsRead(id, mids);
}
auto Client::GetMessageHistoryPage(ContactID const& id, size_t i, size_t s) const -> messaging::HistoryPage {
  return messaging_service_->GetHistoryPage(id, i, s);
}
auto Client::GetFullMessageContent(ContactID const& id, uint64_t msg_id) const -> std::optional<std::string> {
  return messaging_service_->GetFullMessageContent(id, msg_id);
}
auto Client::GetChatSummaries(std::vector<ContactID> const& ids) const -> std::vector<messaging::ChatSummary> {
  return messaging_service_->GetChatSummaries(ids);
}

void Client::Call(ContactID const& id) { Connect(id); }
void Client::StartVoiceCall(ContactID const& id) const { media_call_service_->StartVoiceCall(id); }
void Client::StartVideoCall(ContactID const& id) const { media_call_service_->StartVideoCall(id); }
void Client::AcceptCall(ContactID const& id, messaging::HistoryEntryKind k) const {
  media_call_service_->AcceptCall(id, k);
}
void Client::RejectCall(ContactID const& id) const { media_call_service_->RejectCall(id); }
void Client::Hangup(ContactID const& id, bool n) const { media_call_service_->Hangup(id, n); }
auto Client::GetPeerConnectionState(ContactID const& id) const -> transport::PeerConnectionState {
  return transport_->GetPeerConnectionState(id);
}

void Client::DebugEraseAllHistory() const { storage_->DebugClearAllHistory(); }
void Client::DebugEraseAllContacts() const {
  storage_->DebugClearAllContacts();
  contact_service_->DebugClearCache();  // Added: ensure cache is cleared
  storage_->DebugClearAllHistory();
}
void Client::DebugRevokeAllContactApprovals() const {
  storage_->DebugRevokeAllContactApprovals();
  contact_service_->DebugReloadCache();  // Added: ensure cache is reloaded
}

void Client::SetStatusCallback(StatusCallbackFunction const& cb) {
  status_callback_ = std::make_shared<StatusCallbackFunction>(cb);
}

void Client::HandleProfileInfo(ContactID const& peer_id, Client::Json const& message) {}

}  // namespace eillance
