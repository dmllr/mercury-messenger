#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/candidate.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/memory.h"

namespace eillance {

struct Device {
  struct Camera {
    struct Resolution {
      std::uint16_t width;
      std::uint16_t height;
    };
    std::string id;
    std::string name;
    std::vector<Resolution> resolutions;
  };
  using Profile = std::vector<Camera>;
  std::string id;
  std::string name;
  Profile profile;
};

class Client final : public transport::TransportDelegate {
public:
  using Json = nlohmann::json;
  using Peer = transport::Peer;
  using OnFrameReceivedCallback = std::function<void(const std::vector<std::byte>&)>;
  using OnProfileReceivedCallback = std::function<void(const std::string&, const Device::Profile&)>;
  using OnChatMessageReceivedCallback = std::function<void(const std::string&, const messaging::HistoryEntryRecord&)>;
  using OnContactRequestCallback = std::function<void(const std::string& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(const std::string& peer_id, identity::ContactStatus status)>;
  using OnMessageStatusChangedCallback = std::function<void(const std::string& peer_id, std::uint64_t message_id, messaging::MessageStatus status)>;
  using OnPeerListUpdateCallback = std::function<void(const std::vector<Device>&)>;
  using StatusCallbackFunction = transport::Transport::StatusCallbackFunction;
  using OnCallRingingCallback = std::function<void(const std::string& peer_id)>;
  using OnCallAcceptedCallback = std::function<void(const std::string& peer_id)>;
  using OnIncomingCallCallback = std::function<void(const std::string& peer_id, messaging::HistoryEntryKind call_type)>;
  using OnMyProfileUpdatedCallback = std::function<void()>;
  using OnContactProfileUpdatedCallback = std::function<void(const std::string& peer_id)>;
  using OnCallTerminatedCallback = std::function<void(const std::string& peer_id)>;
  using OnContactRemovedCallback = std::function<void(const std::string& peer_id)>;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport);

  Client(Client&&) = delete;
  Client(const Client&) = delete;
  auto operator=(Client&&) -> Client& = delete;
  auto operator=(const Client&) -> Client& = delete;
  ~Client() override = default;

  // Setter injection for platform-specific capture implementations
  template<typename T>
  [[maybe_unused]] void SetVideoCapture(std::unique_ptr<T> impl);
  template<typename T>
  [[maybe_unused]] void SetAudioCapture(std::unique_ptr<T> impl);

  [[nodiscard]] auto GetId() const -> std::string;
  auto IdentityExists() const -> bool;
  auto GenerateNewIdentity() -> std::string;
  auto GetMnemonic() -> std::optional<std::string>;
  auto RestoreIdentity(const std::string& mnemonic) -> bool;

  auto CreateBackup(const std::string& output_path, const std::string& password) -> bool;
  auto RestoreBackup(const std::string& input_path, const std::string& password) -> bool;

  auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage>;
  auto GetSettingsManager() const -> SettingsManager*;
  void SetName(const std::string& name) const;
  void Start();
  void Terminate();
  void SetStatusCallback(const StatusCallbackFunction& callback);

  [[maybe_unused]] void SetFrameReceivedCallback(const OnFrameReceivedCallback& callback);

  [[maybe_unused]] void SetProfileReceivedCallback(const OnProfileReceivedCallback& callback);
  void SetChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback);
  void SetContactRequestCallback(const OnContactRequestCallback& callback);
  void SetContactStatusChangedCallback(const OnContactStatusChangedCallback& callback);
  void SetMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback);
  void SetPeerListUpdateCallback(const OnPeerListUpdateCallback& callback);
  void SetIncomingCallCallback(const OnIncomingCallCallback& callback);
  void SetOnCallRingingCallback(const OnCallRingingCallback& callback);
  void SetOnCallAcceptedCallback(const OnCallAcceptedCallback& callback);
  void SetOnCallTerminatedCallback(const OnCallTerminatedCallback& callback);
  void SetOnMyProfileUpdatedCallback(const OnMyProfileUpdatedCallback& callback);
  void SetOnContactRemovedCallback(const OnContactRemovedCallback& callback);
  void SetOnContactProfileUpdatedCallback(const OnContactProfileUpdatedCallback& callback);
  void Call(const std::string& peer_id);
  void Connect(const std::string& peer_id);
  void StartVoiceCall(const std::string& peer_id);
  void StartVideoCall(const std::string& peer_id);
  void AcceptCall(const std::string& peer_id, messaging::HistoryEntryKind call_kind) const;
  void RejectCall(const std::string& peer_id) const;
  void AddContact(const std::string& peer_id, const std::optional<std::string>& token = std::nullopt) const;
  auto GenerateContactToken() -> std::string;
  void ConfirmContact(const std::string& peer_id) const;
  void BlockContact(const std::string& peer_id, bool notify_peer = true) const;
  void DeleteContact(const std::string& peer_id) const;
  void UnblockContact(const std::string& peer_id);
  auto GetPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState;
  auto GetContacts() const -> std::vector<identity::Contact>;
  auto GetChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary>;
  void Hangup(const std::string& peer_id) const;
  void Configure(const Config& config) const;
  auto SendChatMessage(const std::string& peer_id, const std::string& message) -> messaging::HistoryEntryRecord;
  void UpdateSignalingUrl(const std::string& url);
  void MarkMessagesAsRead(const std::string& peer_id, const std::vector<std::uint64_t>& message_ids) const;
  auto GetMessageHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage;
  auto GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string>;
  void UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo);
  void SetContactDisplayNameOverride(const std::string& peer_id, const std::string& override_name) const;
  void ResetContactDisplayNameOverride(const std::string& peer_id) const;

  // Debug methods
  void DebugEraseAllHistory() const;
  void DebugEraseAllContacts() const;
  void DebugRevokeAllApprovals() const;

  // TransportDelegate implementation
  void OnPeerReady(const std::shared_ptr<Peer>& peer) override;
  void OnSignalingErrorForPeer(const std::string& peer_id, const std::string& error) override;
  auto Sign(const std::string& message) -> std::vector<std::byte> override;
  void OnLoginSuccess() override;
  void ProcessPeerMessage(Json message) override;
  void OnSystemMessage(const std::string& type, const Json& message) override;
  void OnDataChannelOpen(const std::string& peer_id) override;
  void OnFrameReceived(const std::string& peer_id, const std::vector<std::byte>& frame) override;

private:
  void InitializeAVStream();
  void StartMediaCall(const std::string& peer_id, bool is_video);
  void UpdateAndNotifyPeerList();
  void SendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const;
  void ProcessDataChannelMessage(const std::string& peer_id, const Json& message);
  void SendQueuedMessages(const std::string& peer_id);

  void CheckForInactivePeers();
  void UpdatePeerActivity(const std::string& peer_id);
  DispatchQueue inactivity_check_queue_{"InactiveCheck", 1};
  std::atomic<bool> is_running_{false};

  void SetupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id);
  void HandleProfileUpdate(const std::string& peer_id, const Json& profile_json) const;
  static auto ParseDeviceProfile(const Json& jprofile) -> Device::Profile;

  std::shared_ptr<Stream> av_stream_;
  std::shared_ptr<transport::Transport> transport_;

  std::unique_ptr<Capture> video_capture_;
  std::unique_ptr<Capture> audio_capture_;

  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  std::shared_ptr<OnChatMessageReceivedCallback> on_chat_message_received_{};
  std::shared_ptr<OnContactRequestCallback> on_contact_request_{};
  std::shared_ptr<OnContactStatusChangedCallback> on_contact_status_changed_{};
  std::shared_ptr<OnMessageStatusChangedCallback> on_message_status_changed_{};
  std::shared_ptr<OnPeerListUpdateCallback> on_peer_list_update_{};
  std::shared_ptr<StatusCallbackFunction> status_callback_{};
  std::shared_ptr<OnIncomingCallCallback> on_incoming_call_{};
  std::shared_ptr<OnCallRingingCallback> on_call_ringing_{};
  std::shared_ptr<OnCallAcceptedCallback> on_call_accepted_{};
  std::shared_ptr<OnCallTerminatedCallback> on_call_terminated_{};
  std::shared_ptr<OnMyProfileUpdatedCallback> on_my_profile_updated_{};
  std::shared_ptr<OnContactProfileUpdatedCallback> on_contact_profile_updated_{};
  std::shared_ptr<OnContactRemovedCallback> on_contact_removed_{};

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::IdManager> id_manager_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;
  std::unique_ptr<messaging::MessageHistory> message_history_;

  std::unique_ptr<SettingsManager> settings_manager_;
  std::unique_ptr<backup::BackupManager> backup_manager_;
  std::unordered_map<std::string, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;
  static constexpr std::chrono::seconds kPeerInactivityCheckInterval{30};
  static constexpr std::chrono::minutes kPeerInactivityTimeout{5};
};

// --- Implementation ---

inline Client::Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport) : storage_(std::move(storage)), transport_(std::move(transport)) {
  transport_->setDelegate(this);

  if (auto db_path = storage_->GetPath(); !db_path.empty()) {
    backup_manager_ = std::make_unique<backup::BackupManager>(db_path);
  }

  id_manager_ = std::make_unique<identity::IdManager>(storage_);
  settings_manager_ = std::make_unique<SettingsManager>(storage_);
  contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
  message_history_ = std::make_unique<messaging::MessageHistory>(storage_);

  // Register the status changed callback here to ensure it's always active.
  message_history_->SetStatusChangedCallback([this](const std::string& p_id, std::uint64_t msg_id, messaging::MessageStatus status) {
    if (on_message_status_changed_) { (*on_message_status_changed_)(p_id, msg_id, status); }
  });

  delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(storage_, contact_manager_, [this](const std::string& peer_id) {
    if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
      SendQueuedMessages(peer_id);
    } else if (!peer) {
      Connect(peer_id);
    }
  });

  transport_->SetStatusCallback([this](const std::string& peer_id, const EventSource source, int state_val) {
    if (source == EventSource::peerConnectionState) {
      const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      if (state == eillance::transport::PeerConnectionState::Failed || state == eillance::transport::PeerConnectionState::Closed || state == eillance::transport::PeerConnectionState::Disconnected) {
        // The peer connection is no longer viable. Removing the peer object from the transport layer
        // ensures that a new connection attempt by the MessageDeliveryService will start from a clean state.
        transport_->removePeer(peer_id);
        delivery_service_->connectionAttemptFinished(peer_id);
      }
    }
    if (status_callback_) { (*status_callback_)(peer_id, source, state_val); }
  });
}

template<typename T>
[[maybe_unused]] inline void Client::SetVideoCapture(std::unique_ptr<T> impl) {
  video_capture_ = std::make_unique<Capture>(std::move(impl));
  if (audio_capture_) { InitializeAVStream(); }
}

template<typename T>
[[maybe_unused]] inline void Client::SetAudioCapture(std::unique_ptr<T> impl) {
  audio_capture_ = std::make_unique<Capture>(std::move(impl));
  if (video_capture_) { InitializeAVStream(); }
}

inline void Client::InitializeAVStream() {
  if (!video_capture_ || !audio_capture_) { return; }
  av_stream_ = std::make_shared<Stream>(std::make_shared<StreamSource>(std::move(*video_capture_)), std::make_shared<StreamSource>(std::move(*audio_capture_)));
  av_stream_->OnSample([this](Stream::Source::Type type, uint64_t sample_time, const std::vector<std::byte>& sample) { SendSampleToPeers(sample, type, sample_time); });
}

inline auto Client::GetId() const -> std::string {
  return id_manager_->GetId();
}

inline auto Client::IdentityExists() const -> bool {
  return id_manager_->IdentityExists();
}

inline auto Client::GenerateNewIdentity() -> std::string {
  return id_manager_->GenerateNewIdentity();
}

inline auto Client::GetMnemonic() -> std::optional<std::string> {
  return id_manager_->GetMnemonic();
}

inline auto Client::RestoreIdentity(const std::string& mnemonic) -> bool {
  return id_manager_->RestoreFromMnemonic(mnemonic);
}

inline auto Client::CreateBackup(const std::string& output_path, const std::string& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->CreateBackup(output_path, password);
}

inline auto Client::RestoreBackup(const std::string& input_path, const std::string& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->RestoreFromBackup(input_path, password);
}

inline auto Client::GetStorage() const -> std::shared_ptr<storage::PersistentStorage> {
  return storage_;
}

inline auto Client::GetSettingsManager() const -> SettingsManager* {
  return settings_manager_.get();
}

inline void Client::SetName(const std::string& name) const {
  transport_->SetName(name);
}

inline void Client::Start() {
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

  transport_->setId(id_manager_->GetId());
  transport_->Start();
  inactivity_check_queue_.Dispatch([this] { CheckForInactivePeers(); });
}

inline void Client::Terminate() {
  is_running_ = false;
  delivery_service_->stop();
  transport_->Terminate();
}

inline void Client::SetStatusCallback(const StatusCallbackFunction& callback) {
  status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
}

[[maybe_unused]] inline void Client::SetFrameReceivedCallback(const OnFrameReceivedCallback& callback) { on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback); }

[[maybe_unused]] inline void Client::SetProfileReceivedCallback(const OnProfileReceivedCallback& callback) { on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback); }

inline void Client::SetChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback) {
  on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback);
}

inline void Client::SetContactRequestCallback(const OnContactRequestCallback& callback) {
  on_contact_request_ = std::make_shared<OnContactRequestCallback>(callback);
}

inline void Client::SetContactStatusChangedCallback(const OnContactStatusChangedCallback& callback) {
  on_contact_status_changed_ = std::make_shared<OnContactStatusChangedCallback>(callback);
}

inline void Client::SetMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback) {
  on_message_status_changed_ = std::make_shared<OnMessageStatusChangedCallback>(callback);
}

inline void Client::SetPeerListUpdateCallback(const OnPeerListUpdateCallback& callback) {
  on_peer_list_update_ = std::make_shared<OnPeerListUpdateCallback>(callback);
}

inline void Client::SetIncomingCallCallback(const OnIncomingCallCallback& callback) {
  on_incoming_call_ = std::make_shared<OnIncomingCallCallback>(callback);
}

inline void Client::SetOnCallRingingCallback(const OnCallRingingCallback& callback) {
  on_call_ringing_ = std::make_shared<OnCallRingingCallback>(callback);
}

inline void Client::SetOnCallAcceptedCallback(const OnCallAcceptedCallback& callback) {
  on_call_accepted_ = std::make_shared<OnCallAcceptedCallback>(callback);
}

inline void Client::SetOnCallTerminatedCallback(const OnCallTerminatedCallback& callback) {
  on_call_terminated_ = std::make_shared<OnCallTerminatedCallback>(callback);
}

inline void Client::SetOnMyProfileUpdatedCallback(const OnMyProfileUpdatedCallback& callback) {
  on_my_profile_updated_ = std::make_shared<OnMyProfileUpdatedCallback>(callback);
}

inline void Client::SetOnContactRemovedCallback(const OnContactRemovedCallback& callback) {
  on_contact_removed_ = std::make_shared<OnContactRemovedCallback>(callback);
}

inline void Client::SetOnContactProfileUpdatedCallback(const OnContactProfileUpdatedCallback& callback) {
  on_contact_profile_updated_ = std::make_shared<OnContactProfileUpdatedCallback>(callback);
}

inline void Client::Call(const std::string& peer_id) {
  Connect(peer_id);
}

inline void Client::Connect(const std::string& peer_id) {
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

inline void Client::StartVoiceCall(const std::string& peer_id) {
  StartMediaCall(peer_id, false);
}

inline void Client::StartVideoCall(const std::string& peer_id) {
  StartMediaCall(peer_id, true);
}

inline void Client::AcceptCall(const std::string& peer_id, messaging::HistoryEntryKind call_kind) const {
  auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    LOGW << "Cannot accept call from " << peer_id << ", peer object not found.";
    return;
  }

  nlohmann::json msg = {{"action", "call_accept"}, {"kind", call_kind}};
  message_history_->AddTextEntry(peer_id, msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);
  delivery_service_->attemptDelivery(peer_id);
}

inline void Client::RejectCall(const std::string& peer_id) const {
  Hangup(peer_id);
}

inline void Client::AddContact(const std::string& peer_id, const std::optional<std::string>& token) const {
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

inline auto Client::GenerateContactToken() -> std::string {
  return contact_manager_->GenerateContactToken();
}

inline void Client::DeleteContact(const std::string& peer_id) const {
  // Close any active connections and remove the peer object.
  if (auto peer = transport_->getPeer(peer_id)) {
    transport_->closePeerConnection(peer_id, "data");
    transport_->closePeerConnection(peer_id, "media");
    transport_->removePeer(peer_id);
  }
  contact_manager_->DeleteContact(peer_id);
}

inline void Client::BlockContact(const std::string& peer_id, bool notify_peer) const {
  if (notify_peer) {
    if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_) {
      const Json msg = {{"type", "contact_removed"}};
      // This is a fire-and-forget message. We don't use the delivery service because
      // we are about to tear down the connection and remove the contact entirely.
      transport_->sendPeerDataChannelMessage(peer_id, msg);
    }
  }

  // Close any active connections and remove the peer object.
  if (auto peer = transport_->getPeer(peer_id)) {
    transport_->closePeerConnection(peer_id, "data");
    transport_->closePeerConnection(peer_id, "media");
    transport_->removePeer(peer_id);
  }
  contact_manager_->BlockContact(peer_id);
}

inline void Client::UnblockContact(const std::string& peer_id) {
  if (contact_manager_->UnblockContact(peer_id)) {
    if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::awaitingApproval); }
  }
}

inline void Client::ConfirmContact(const std::string& peer_id) const {
  if (contact_manager_->ConfirmContact(peer_id)) {
    // 1. Send confirmation back to peer_id (this peer) via signaling
    const Json content = {{"action", "contact_confirm"}};
    message_history_->AddTextEntry(peer_id, content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    // 2. Create signature for them (this peer signs the other's ID)
    auto signature_bytes = id_manager_->Sign(peer_id);
    auto signature_hex = crypto::ToHex(signature_bytes);
    contact_manager_->StoreMySignatureFor(peer_id, signature_hex);

    // 3. Send signature to them via data channel
    const Json sig_content = {{"action", "contact_signature"}, {"signature", signature_hex}};
    message_history_->AddTextEntry(peer_id, sig_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    const Json profile_content = {{"action", "send_profile"}};
    message_history_->AddTextEntry(peer_id, profile_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

    if (on_contact_status_changed_) { (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed); }
    delivery_service_->attemptDelivery(peer_id);
  }
}

inline auto Client::GetPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState {
  return transport_->getPeerConnectionState(peer_id);
}

inline auto Client::GetContacts() const -> std::vector<identity::Contact> {
  return contact_manager_->GetContacts();
}

inline auto Client::GetChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary> {
  return message_history_->GetChatSummaries(peer_ids);
}

inline void Client::Hangup(const std::string& peer_id) const {
  if (const auto peer = transport_->getPeer(peer_id); peer != nullptr) {
    const bool call_was_active = peer->pending_offer_sdp.has_value() || peer->has_media_peer_connection_;
    peer->pending_offer_sdp.reset();
    if (!peer->pending_candidates.empty()) { peer->pending_candidates.clear(); }

    // If a media connection exists, close it. This will trigger the onStateChange callback
    // which is the primary mechanism for updating the UI that the call has ended.
    if (peer->has_media_peer_connection_) {
      LOGI << "Closing media peer connection for " << peer_id;
      transport_->closePeerConnection(peer_id, "media");
      peer->has_media_peer_connection_ = false;
      peer->audio_ = nullptr;
      peer->video_ = nullptr;
      // On platforms with custom capture (like CLI), stop the media stream.
      if (av_stream_) { av_stream_->Stop(); }
    }

    // If the call was active in any state, notify the other peer.
    if (call_was_active) {
      LOGI << "Sending hangup signal to " << peer_id << " via data channel.";
      if (peer && peer->data_channel_is_open_) {
        const Json msg = {{"type", "hangup"}};
        transport_->sendPeerDataChannelMessage(peer_id, msg);
      } else {
        LOGW << "Cannot send hangup message to " << peer_id << "; data channel is not open.";
      }
    } else {
      LOGW << "Hangup called for " << peer_id << ", but no active call state (ringing or connected) was found.";
    }
  }
}

inline void Client::Configure(const Config& config) const {
  if (av_stream_) {
    av_stream_->Video()->Configure(config);
    av_stream_->Audio()->Configure(config);
  }
}

inline void Client::OnPeerReady(const std::shared_ptr<Peer>&) {}

inline auto Client::Sign(const std::string& message) -> std::vector<std::byte> {
  return id_manager_->Sign(message);
}

inline void Client::OnLoginSuccess() {
  delivery_service_->Start();
}

inline void Client::OnSystemMessage(const std::string& type, const Json& message) {  // NOLINT(*-cognitive-complexity)
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
      (*status_callback_)("", EventSource::system, static_cast<int>(ServiceConnectionState::online));
    }
  } else {
    LOGW << "Received unknown system message of type: " << type;
  }
}

inline void Client::OnSignalingErrorForPeer(const std::string& peer_id, const std::string& error) {
  if (error == "peer_not_found") {
    LOGW << "Connection attempt failed for peer " << peer_id << " because they are not online. Will retry when they appear.";
    delivery_service_->connectionAttemptFinished(peer_id);
    if (transport_->getPeer(peer_id)) { transport_->closePeerConnection(peer_id, "data"); }
  }
}

inline void Client::OnDataChannelOpen(const std::string& peer_id) {
  auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    LOGW << "onDataChannelOpen called for non-existent peer: " << peer_id;
    return;
  }
  UpdatePeerActivity(peer_id);
  LOGI << "Data channel with " << peer_id << " opened.";
  peer->data_channel_is_open_ = true;

  if (GetId() > peer_id) {
    auto my_pub_key = peer->session_cipher_->GetPublicKey();
    const Json key_exchange_message = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", false}};
    transport_->sendPeerDataChannelMessage(peer_id, key_exchange_message);
  }
}

inline void Client::ProcessPeerMessage(Json message) {  // NOLINT(*-cognitive-complexity)
  const auto from_it = message.find("from");
  if (from_it == message.end()) { return; }
  auto peer_id = from_it->get<std::string>();
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
        // After removing, the 'peer' pointer is stale. Set to null to fall through to new connection logic.
        peer = nullptr;
      } else {  // This peer is "impolite"
        LOGW << "Glare detected, and we are the 'impolite' peer. Ignoring incoming offer from " << peer_id;
        return;  // Let our outgoing offer attempt proceed.
      }
    }

    // This block is now executed for new connections OR after a polite glare-handling reset.
    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      transport_->addPeer(peer_id, peer);
      SetupDataChannel(peer, peer_id);  // Set callback before creating the PC
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

      // Find the original request and mark it as applied.
      auto pending_requests = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sent);
      for (const auto& req: pending_requests) {
        if (req.kind == messaging::HistoryEntryKind::systemMessage) {
          const auto& content_str = std::get<messaging::MessageEntry>(req.data).content;
          try {
            Json content_json = Json::parse(content_str);
            if (content_json.value("action", "") == "contact_request") {
              message_history_->UpdateMessageStatus(peer_id, req.id, messaging::MessageStatus::read);
              break;  // Assume only one pending request needs to be marked.
            }
          } catch (...) {
            // TODO
          }
        }
      }

      // Send our profile back upon confirmation.
      const Json profile_content = {{"action", "send_profile"}};
      message_history_->AddTextEntry(peer_id, profile_content.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

      // Also request the peer's profile to ensure a two-way exchange,
      // mitigating a race condition where their initial profile send might be dropped.
      const Json request_content = {{"action", "request_profile"}};
      message_history_->AddTextEntry(peer_id, request_content.dump(), messaging::MessageDirection::sent,
                                     messaging::HistoryEntryKind::systemMessage);

      delivery_service_->attemptDelivery(peer_id);
    }
  }
}

inline void Client::OnFrameReceived(const std::string& /*peer_id*/, const std::vector<std::byte>& frame) {
  if (on_frame_received_) { (*on_frame_received_)(frame); }
}

inline auto Client::SendChatMessage(const std::string& peer_id, const std::string& message) -> messaging::HistoryEntryRecord {
  if (!contact_manager_->GetContact(peer_id)) {
    LOGE << "Cannot send message to " << peer_id.c_str() << ". Not a contact.";
    return {};
  }
  auto entry = message_history_->AddTextEntry(peer_id, message, messaging::MessageDirection::sent);

  // If the peer is already connected and ready, send immediately.
  // Otherwise, let the delivery service handle connection and subsequent sending.
  if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->attemptDelivery(peer_id);
  }

  return entry;
}

inline void Client::UpdateSignalingUrl(const std::string& url) {
    if (is_running_) {
        settings_manager_->SetSignalingServerUrl(url);
        transport_->setSignalingUrl(url);
        transport_->reconnect();
    } else {
        LOGW << "UpdateSignalingUrl called while client is not running. URL will be applied on next start.";
        settings_manager_->SetSignalingServerUrl(url);
    }
}
inline void Client::MarkMessagesAsRead(const std::string& peer_id, const std::vector<std::uint64_t>& message_ids) const {
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
inline auto Client::GetMessageHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage {
  return message_history_->GetHistoryPage(peer_id, page_index, page_size);
}

inline auto Client::GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string> {
  return message_history_->GetFullMessageContent(message_id);
}

inline void Client::UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
  if (name) { settings_manager_->SetMyDisplayName(*name); }
  if (bio) { settings_manager_->SetMyBio(*bio); }
  if (photo) { settings_manager_->SetMyPhotoBase64(*photo); }

  if (on_my_profile_updated_) {
    (*on_my_profile_updated_)();
  }

  // Immediately notify all confirmed contacts of the profile change.
  for (const auto& contact: contact_manager_->GetContacts()) {
    if (contact.status == identity::ContactStatus::confirmed) {
      Json profile_update_msg;
      profile_update_msg["action"] = "profile_update";
      profile_update_msg["display_name"] = settings_manager_->GetMyDisplayName().value_or("");
      profile_update_msg["bio"] = settings_manager_->GetMyBio().value_or("");
      profile_update_msg["photo"] = settings_manager_->GetMyPhotoBase64();

      message_history_->AddTextEntry(contact.peer_id, profile_update_msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

      if (auto peer = transport_->getPeer(contact.peer_id); peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
        SendQueuedMessages(contact.peer_id);
      } else {
        delivery_service_->attemptDelivery(contact.peer_id);
      }
    }
  }
}

inline void Client::SetContactDisplayNameOverride(const std::string& peer_id, const std::string& override_name) const {
  contact_manager_->SetDisplayNameOverride(peer_id, override_name);
  if (on_contact_profile_updated_) { (*on_contact_profile_updated_)(peer_id); }
}

inline void Client::ResetContactDisplayNameOverride(const std::string& peer_id) const {
  contact_manager_->ResetDisplayNameOverride(peer_id);
  if (auto peer = transport_->getPeer(peer_id); peer && peer->data_channel_is_open_) {
    const Json request = {{"type", "profile_request"}};
    transport_->sendPeerDataChannelMessage(peer_id, request);
  }
  if (on_contact_profile_updated_) { (*on_contact_profile_updated_)(peer_id); }
}

inline void Client::DebugEraseAllHistory() const {
  storage_->DebugClearAllHistory();
}

inline void Client::DebugEraseAllContacts() const {
  storage_->DebugClearAllContacts();
  // Also clear history as it's tied to contacts
  storage_->DebugClearAllHistory();
}

inline void Client::DebugRevokeAllApprovals() const {
  storage_->DebugRevokeAllContactApprovals();
}

inline void Client::StartMediaCall(const std::string& peer_id, const bool is_video) {
  const auto call_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

  // Create the call request message first and queue it for delivery.
  nlohmann::json msg = {{"action", "call_request"}, {"kind", call_kind}};
  message_history_->AddTextEntry(peer_id, msg.dump(), messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

  // Now, trigger the delivery mechanism.
  auto peer = transport_->getPeer(peer_id);
  if (peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
    // If already connected, just send the queue.
    SendQueuedMessages(peer_id);
  } else {
    // If not connected, let the delivery service handle establishing the connection.
    // The message we just added will cause it to trigger a connection attempt.
    delivery_service_->attemptDelivery(peer_id);
  }
}

inline void Client::UpdateAndNotifyPeerList() {
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

inline void Client::SendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const {
  for (const auto& [id, peer]: transport_->getPeers()) {
    if (peer->GetState() == Peer::State::ready) {
      auto track = (type == Stream::Source::Type::video) ? peer->video_ : peer->audio_;
      if (track) { track->SendSample(sample, sample_time); }
    }
  }
}

inline void Client::UpdatePeerActivity(const std::string& peer_id) {
  if (auto peer = transport_->getPeer(peer_id)) {
    peer->last_activity_time_ = Time::Now();
  }
}

inline void Client::CheckForInactivePeers() {
  if (!is_running_) { return; }

  auto peers = transport_->getPeers();
  auto now = Time::Now();
  const auto timeout_us = std::chrono::duration_cast<std::chrono::microseconds>(kPeerInactivityTimeout).count();

  for (const auto& [peer_id, peer] : peers) {
    if (!peer->has_media_peer_connection_ && GetPeerConnectionState(peer_id) == transport::PeerConnectionState::Connected) {
      if ((now - peer->last_activity_time_.load()) > timeout_us) {
        LOGI << "Peer " << peer_id << " inactive for too long. Closing data connection.";
        transport_->closePeerConnection(peer_id, "data");
      }
    }
  }
  inactivity_check_queue_.DispatchAfter(kPeerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
}

inline void Client::HandleProfileUpdate(const std::string& peer_id, const Json& profile_json) const {
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

inline void Client::ProcessDataChannelMessage(const std::string& peer_id, const Json& message) {
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

  // Handle messages that DO NOT require a confirmed contact or session cipher.
  if (type == "key_exchange") {
    auto pub_key_bytes = message.at("key").get<std::vector<std::byte>>();
    if (peer->session_cipher_->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
      LOGI << "Successfully computed shared secret with peer " << peer_id;
      const bool is_reply = message.value("is_reply", false);
      if (!is_reply) {
        // This is an initial key exchange request from the peer with the greater ID.
        // We compute the secret and send back a reply. We DO NOT send queued messages yet.
        auto my_pub_key = peer->session_cipher_->GetPublicKey();
        const Json reply = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", true}};
        transport_->sendPeerDataChannelMessage(peer_id, reply);
      } else {
        // This is a reply to our key exchange request. We are the initiator.
        // The handshake is complete, and we can now send our queued messages.
        SendQueuedMessages(peer_id);
      }
    } else {
      LOGE << "Failed to compute shared secret with peer " << peer_id;
    }
    return;
  }

  if (type == "hangup") {
    LOGI << "Received hangup signal from " << peer_id << " via data channel.";
    Hangup(peer_id);
    if (on_call_terminated_) { (*on_call_terminated_)(peer_id); }
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

    if (crypto::Identity::Verify(peer_id, GetId(), signature_bytes)) {
      LOGI << "Re-confirmation from " << peer_id << " successful. Re-adding as contact.";
      contact_manager_->MarkAsConfirmed(peer_id);

      const auto my_signature_bytes = id_manager_->Sign(peer_id);
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
    if (crypto::Identity::Verify(peer_id, GetId(), signature_bytes)) {
      LOGI << "Received and verified contact signature from " << peer_id;
      contact_manager_->StoreSignatureFrom(peer_id, signature_hex);
    } else {
      LOGW << "Received invalid contact signature from " << peer_id;
    }
    return;
  }

  // For all other messages, require the sender to be a confirmed contact.
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
      online_peers_[peer_id].id = peer_id;
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
    if (is_system) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
      if (decrypted_message.empty()) {
        LOGW << "Failed to decrypt system message from " << peer_id;
        return;
      }
      const auto content_json = Json::parse(decrypted_message);
      const auto action = content_json.value("action", "");
      if (action == "contact_signature") {
        auto signature_hex = content_json.at("signature").get<std::string>();
        auto signature_bytes = crypto::FromHex(signature_hex);
        if (crypto::Identity::Verify(peer_id, GetId(), signature_bytes)) {
          LOGI << "Received and verified contact signature from " << peer_id;
          contact_manager_->StoreSignatureFrom(peer_id, signature_hex);
          auto my_contact_info_for_them = contact_manager_->GetContact(peer_id);
          if (!my_contact_info_for_them || !my_contact_info_for_them->my_signature_for_them) {
            auto my_signature_bytes = id_manager_->Sign(peer_id);
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
          messaging::HistoryEntryKind call_type = content_json.value("kind", messaging::HistoryEntryKind::voiceCall);
          (*on_incoming_call_)(peer_id, call_type);

          // Acknowledge the request by sending a "ringing" message back
          Json ringing_msg = {{"action", "call_ringing"}};
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
    } else if (on_chat_message_received_) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        auto history_entry = message_history_->AddTextEntry(peer_id, decrypted_message, messaging::MessageDirection::received);
        (*on_chat_message_received_)(peer_id, history_entry);
      } else {
        LOGW << "Failed to decrypt message from " << peer_id;
      }
    }

    // ALWAYS send receipt after processing
    const Json receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    transport_->sendPeerDataChannelMessage(peer_id, receipt);

    // After successfully processing the first encrypted message, we know the other side is ready.
    // We can now send our own queued messages. This is safe to call multiple times as it's idempotent.
    SendQueuedMessages(peer_id);

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

    // It's important to always send the full photo on a request, not just on update.
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

inline void Client::SendQueuedMessages(const std::string& peer_id) {
  delivery_service_->connectionAttemptFinished(peer_id);
  const auto messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  if (messages.empty()) { return; }
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
}

inline void Client::SetupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id) {
  peer->on_data_channel_message_ = [this, peer_id](const std::string& msg) {
    ProcessDataChannelMessage(peer_id, nlohmann::json::parse(msg));
  };
}

inline auto Client::ParseDeviceProfile(const Json& jprofile) -> Device::Profile {
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
