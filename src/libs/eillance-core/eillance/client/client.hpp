#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "eillance/client/device.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance {

class Client final : public transport::TransportDelegate {
 public:
  using Json = nlohmann::json;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport)
      : storage_(std::move(storage)),
        transport_(std::move(transport)) {
    transport_->SetDelegate(this);

    // Initialize Services
    account_service_ = std::make_shared<services::AccountService>(storage_, transport_);

    // Shared ContactManager
    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

    // Initialize FileTransferService
    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

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
        file_transfer_service_,
        [this](ContactID const& id) { this->Connect(id); }  // Inject connection logic
    );

    // Wire auto-restored contact callback
    messaging_service_->SetContactRestoredCallback([this](ContactID const& id) {
      contact_service_->OnContactRestored(id);
    });

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

  Client(Client const&) = delete;
  Client(Client&&) = delete;
  auto operator=(Client const&) -> Client& = delete;
  auto operator=(Client&&) -> Client& = delete;
  ~Client() override { Terminate(); }

  // --- Service Accessors ---
  [[nodiscard]] auto GetAccountService() const -> std::shared_ptr<services::AccountService> { return account_service_; }
  [[nodiscard]] auto GetContactService() const -> std::shared_ptr<services::ContactService> { return contact_service_; }
  [[nodiscard]] auto GetMessagingService() const -> std::shared_ptr<services::MessagingService> {
    return messaging_service_;
  }
  [[nodiscard]] auto GetMediaCallService() const -> std::shared_ptr<services::MediaCallService> {
    return media_call_service_;
  }
  [[nodiscard]] auto GetFileTransferService() const -> std::shared_ptr<services::FileTransferService> {
    return file_transfer_service_;
  }
  [[nodiscard]] auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage> { return storage_; }

  // --- Platform Injection ---
  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    media_call_service_->SetVideoCapture(std::move(impl));
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    media_call_service_->SetAudioCapture(std::move(impl));
  }

  // --- Lifecycle ---
  void Start() {
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

  void Terminate() {
    is_running_ = false;
    messaging_service_->StopDeliveryService();
    transport_->Terminate();
  }

  void Configure(Config const& config) const { media_call_service_->Configure(config); }

  // --- Facade Methods ---

  [[nodiscard]] auto GetId() const -> ContactID { return account_service_->GetId(); }
  [[nodiscard]] auto IdentityExists() const -> bool { return account_service_->IdentityExists(); }
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> { return account_service_->GetMnemonic(); }

  auto GenerateNewIdentity() -> std::string {
    auto mnemonic = account_service_->GenerateNewIdentity();
    BroadcastProfileUpdate();
    return mnemonic;
  }

  auto RestoreIdentity(std::string const& m) -> bool {
    if (account_service_->RestoreIdentity(m)) {
      BroadcastProfileUpdate();
      return true;
    }
    return false;
  }

  [[nodiscard]] auto CreateBackup(std::string const& p, std::string const& pw) const -> bool {
    return account_service_->CreateBackup(p, pw);
  }

  auto RestoreBackup(std::string const& p, std::string const& pw) -> bool {
    if (account_service_->RestoreBackup(p, pw)) {
      BroadcastProfileUpdate();
      return true;
    }
    return false;
  }

  void SetName(std::string const& name) const { transport_->SetName(name); }
  void UpdateSignalingUrl(std::string const& url) const { account_service_->UpdateSignalingUrl(url); }

  void UpdateMyProfile(
      std::optional<std::string> const& n,
      std::optional<std::string> const& b,
      std::optional<std::string> const& p
  ) {
    account_service_->UpdateMyProfile(n, b, p);
    BroadcastProfileUpdate();
  }

  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager* { return account_service_->GetSettings(); }

  void AddContact(ContactID const& id, std::optional<std::vector<std::byte>> const& t) const {
    contact_service_->AddContact(id, t);
  }
  auto GenerateContactToken() const -> std::vector<std::byte> { return contact_service_->GenerateContactToken(); }
  void ConfirmContact(ContactID const& id) const { contact_service_->ConfirmContact(id); }
  void BlockContact(ContactID const& id, bool n) const { contact_service_->BlockContact(id, n); }
  void DeleteContact(ContactID const& id) const { contact_service_->DeleteContact(id); }
  void UnblockContact(ContactID const& id) const { contact_service_->UnblockContact(id); }
  void SetContactDisplayNameOverride(ContactID const& id, std::string const& n) const {
    contact_service_->SetContactDisplayNameOverride(id, n);
  }
  void ResetContactDisplayNameOverride(ContactID const& id) const {
    contact_service_->ResetContactDisplayNameOverride(id);
  }
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact> { return contact_service_->GetContacts(); }

  auto SendChatMessage(ContactID const& id, std::string const& msg) const -> messaging::HistoryEntryRecord {
    return messaging_service_->SendChatMessage(id, msg);
  }
  void MarkMessagesAsRead(ContactID const& id, std::vector<std::uint64_t> const& mids) const {
    messaging_service_->MarkMessagesAsRead(id, mids);
  }
  [[nodiscard]] auto GetMessageHistoryPage(ContactID const& id, size_t i, size_t s) const -> messaging::HistoryPage {
    return messaging_service_->GetHistoryPage(id, i, s);
  }
  [[nodiscard]] auto GetFullMessageContent(ContactID const& id, uint64_t msg_id) const -> std::optional<std::string> {
    return messaging_service_->GetFullMessageContent(id, msg_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& ids) const -> std::vector<messaging::ChatSummary> {
    return messaging_service_->GetChatSummaries(ids);
  }

  void CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) const {
    messaging_service_->CancelTransfer(peer_id, attachment_id, is_upload);
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) const {
    messaging_service_->ResumeTransfer(peer_id, attachment_id, is_upload);
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) const {
    messaging_service_->DiscardAttachment(peer_id, attachment_id);
  }

  void Call(ContactID const& id) { Connect(id); }
  void Connect(ContactID const& peer_id) {
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

  void StartVoiceCall(ContactID const& id) const { media_call_service_->StartVoiceCall(id); }
  void StartVideoCall(ContactID const& id) const { media_call_service_->StartVideoCall(id); }
  void AcceptCall(ContactID const& id, messaging::HistoryEntryKind k) const { media_call_service_->AcceptCall(id, k); }
  void RejectCall(ContactID const& id) const { media_call_service_->RejectCall(id); }
  void Hangup(ContactID const& id, bool n) const { media_call_service_->Hangup(id, n); }
  [[nodiscard]] auto GetPeerConnectionState(ContactID const& id) const -> transport::PeerConnectionState {
    return transport_->GetPeerConnectionState(id);
  }

  void DebugEraseAllHistory() const { storage_->DebugClearAllHistory(); }
  void DebugEraseAllContacts() const {
    storage_->DebugClearAllContacts();
    contact_service_->DebugClearCache();
    storage_->DebugClearAllHistory();
  }
  void DebugRevokeAllContactApprovals() const {
    storage_->DebugRevokeAllContactApprovals();
    contact_service_->DebugReloadCache();
  }

  using StatusCallbackFunction = transport::Transport::StatusCallbackFunction;
  void SetStatusCallback(StatusCallbackFunction const& cb) {
    status_callback_ = std::make_shared<StatusCallbackFunction>(cb);
  }

 private:
  void OnPeerReady(std::shared_ptr<transport::Peer> const&) override {}

  void OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) override {
    if (error == "peer_not_found") {
      LGW << "Connection attempt failed for peer " << peer_id.ToString() << " (not online).";
      messaging_service_->OnConnectionAttemptFinished(peer_id);
      if (transport_->GetPeer(peer_id)) {
        transport_->ClosePeerConnection(peer_id, "data");
      }
    }
  }

  auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> override {
    return account_service_->Sign(message);
  }

  void OnLoginSuccess() override {
    messaging_service_->StartDeliveryService();
    if (status_callback_) {
      (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
    }
  }

  void ProcessPeerMessage(Json message) override {
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

  void OnSystemMessage(std::string const& type, Json const& message) override {
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
    } else {
      LGW << "Received unknown system message of type: " << type;
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) override {
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

    // Check for any pending work (messages or profile sync)
    messaging_service_->SendQueuedMessages(peer_id);
    contact_service_->SyncProfile(peer_id);
  }

  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) override {
    media_call_service_->OnFrameReceived(peer_id, frame);
  }

  void CheckForInactivePeers() {
    if (!is_running_) {
      return;
    }
    auto const now = Time::Now();
    auto const timeout = std::chrono::duration_cast<std::chrono::microseconds>(
                             account_service_->GetSettings()->GetPeerInactivityTimeout()
    )
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

  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
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
              {{"type", "key_exchange"},
               {"key", std::vector<std::byte>(my_key.begin(), my_key.end())},
               {"is_reply", true}
              }
          );
        }
        messaging_service_->SendQueuedMessages(peer_id);
        contact_service_->SyncProfile(peer_id);
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
          att.hash = j_att.value("hash", "");
          att.is_downloaded = false;  // It's a remote file
          att.status = messaging::AttachmentStatus::pending;
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
    } else if (type == "file_chunk" || type == "file_cancel" || type == "file_resume_request") {
      file_transfer_service_->ProcessMessage(peer_id, message);
    }
  }

  void SetupPeerHandlers(std::shared_ptr<transport::Peer> const& peer, ContactID const& peer_id) {
    peer->on_data_channel_message = [this, peer_id](std::string const& msg) {
      try {
        ProcessDataChannelMessage(peer_id, Json::parse(msg));
      } catch (...) {
        LGE << "Failed to parse data channel message from " << peer_id.ToString();
      }
    };
  }

  void BroadcastProfileUpdate() {
    contact_service_->MarkAllContactsProfileSyncNeeded();
    // Trigger work check for all contacts
    auto const contacts = contact_service_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
        // Attempt to sync immediately if connection is alive.
        // SyncProfile checks internal state and sends if possible.
        contact_service_->SyncProfile(contact.peer_id);

        // Ensure we attempt to connect if not already connected.
        // If connected, Connect() returns early.
        Connect(contact.peer_id);
      }
    }
  }

  static void HandleProfileInfo(ContactID const&, Json const&) {}

  void SendProfileInfo(ContactID const& peer_id) { contact_service_->SyncProfile(peer_id); }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;

  DispatchQueue background_queue_{"ClientBgQueue", 1};
  std::atomic<bool> is_running_{false};

  std::shared_ptr<StatusCallbackFunction> status_callback_;

  static constexpr std::chrono::seconds kPeerInactivityCheckInterval{30};
};

}  // namespace eillance
