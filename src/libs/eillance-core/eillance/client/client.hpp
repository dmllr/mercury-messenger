#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"

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
  using OnProfileReceivedCallback = std::function<void(const ContactID&, const Device::Profile&)>;
  using OnChatMessageReceivedCallback = std::function<void(const ContactID&, const messaging::HistoryEntryRecord&)>;
  using OnContactRequestCallback = std::function<void(const ContactID& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(const ContactID& peer_id, identity::ContactStatus status)>;
  using OnMessageStatusChangedCallback = std::function<void(const ContactID& peer_id, std::uint64_t message_id, messaging::MessageStatus status)>;
  using OnPeerListUpdateCallback = std::function<void(const std::vector<Device>&)>;
  using StatusCallbackFunction = transport::Transport::StatusCallbackFunction;
  using OnCallRingingCallback = std::function<void(const ContactID& peer_id)>;
  using OnCallAcceptedCallback = std::function<void(const ContactID& peer_id)>;
  using OnIncomingCallCallback = std::function<void(const ContactID& peer_id, messaging::HistoryEntryKind call_type)>;
  using OnMyProfileUpdatedCallback = std::function<void()>;
  using OnContactProfileUpdatedCallback = std::function<void(const ContactID& peer_id)>;
  using OnCallTerminatedCallback = std::function<void(const ContactID& peer_id)>;
  using OnContactRemovedCallback = std::function<void(const ContactID& peer_id)>;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport);

  Client(Client&&) = delete;
  Client(const Client&) = delete;
  auto operator=(Client&&) -> Client& = delete;
  auto operator=(const Client&) -> Client& = delete;
  ~Client() override;

  // Setter injection for platform-specific capture implementations
  template<typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    video_capture_ = std::make_unique<Capture>(std::move(impl));
    if (audio_capture_) { InitializeAVStream(); }
  }

  template<typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    audio_capture_ = std::make_unique<Capture>(std::move(impl));
    if (video_capture_) { InitializeAVStream(); }
  }

  [[nodiscard]] auto GetId() const -> ContactID;
  [[nodiscard]] auto IdentityExists() const -> bool;
  auto GenerateNewIdentity() -> std::string;
  [[nodiscard]] auto GetMnemonic() -> std::optional<std::string>;
  auto RestoreIdentity(const std::string& mnemonic) -> bool;

  auto CreateBackup(const std::string& output_path, const std::string& password) -> bool;
  auto RestoreBackup(const std::string& input_path, const std::string& password) -> bool;

  [[nodiscard]] auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage>;
  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager*;
  void SetName(const std::string& name) const;
  void Start();
  void Terminate();
  void SetStatusCallback(const StatusCallbackFunction& callback);

  void SetFrameReceivedCallback(const OnFrameReceivedCallback& callback);
  void SetProfileReceivedCallback(const OnProfileReceivedCallback& callback);
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

  void Call(const ContactID& peer_id);
  void Connect(const ContactID& peer_id);
  void StartVoiceCall(const ContactID& peer_id);
  void StartVideoCall(const ContactID& peer_id);
  void AcceptCall(const ContactID& peer_id, messaging::HistoryEntryKind call_kind) const;
  void RejectCall(const ContactID& peer_id) const;
  void AddContact(const ContactID& peer_id, const std::optional<std::vector<std::byte>>& token = std::nullopt) const;
  auto GenerateContactToken() -> std::vector<std::byte>;
  void ConfirmContact(const ContactID& peer_id) const;
  void BlockContact(const ContactID& peer_id, bool notify_peer = true) const;
  void DeleteContact(const ContactID& peer_id) const;
  void UnblockContact(const ContactID& peer_id);
  [[nodiscard]] auto GetPeerConnectionState(const ContactID& peer_id) const -> eillance::transport::PeerConnectionState;
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact>;
  [[nodiscard]] auto GetChatSummaries(const std::vector<ContactID>& peer_ids) const -> std::vector<messaging::ChatSummary>;
  void Hangup(const ContactID& peer_id, bool notify_peer = true) const;
  void Configure(const Config& config) const;
  auto SendChatMessage(const ContactID& peer_id, const std::string& message) -> messaging::HistoryEntryRecord;
  void UpdateSignalingUrl(const std::string& url) const;
  void MarkMessagesAsRead(const ContactID& peer_id, const std::vector<std::uint64_t>& message_ids) const;
  [[nodiscard]] auto GetMessageHistoryPage(const ContactID& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage;
  [[nodiscard]] auto GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string>;
  void UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo);
  void SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& override_name) const;
  void ResetContactDisplayNameOverride(const ContactID& peer_id) const;

  // Debug methods
  void DebugEraseAllHistory() const;
  void DebugEraseAllContacts() const;
  void DebugRevokeAllApprovals() const;

  // TransportDelegate implementation
  void OnPeerReady(const std::shared_ptr<Peer>& peer) override;
  void OnSignalingErrorForPeer(const ContactID& peer_id, const std::string& error) override;
  auto Sign(std::span<const std::byte> message) -> std::vector<std::byte> override;
  void OnLoginSuccess() override;
  void ProcessPeerMessage(Json message) override;
  void OnSystemMessage(const std::string& type, const Json& message) override;
  void OnDataChannelOpen(const ContactID& peer_id) override;
  void OnFrameReceived(const ContactID& peer_id, const std::vector<std::byte>& frame) override;

private:
  void InitializeAVStream();
  void StartMediaCall(const ContactID& peer_id, bool is_video);
  void UpdateAndNotifyPeerList();
  void SendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const;
  void ProcessDataChannelMessage(const ContactID& peer_id, const Json& message);
  void SendQueuedMessages(const ContactID& peer_id);

  void CheckForInactivePeers();
  void UpdatePeerActivity(const ContactID& peer_id);

  void SetupDataChannel(const std::shared_ptr<Peer>& peer, const ContactID& peer_id);
  void HandleProfileUpdate(const ContactID& peer_id, const Json& profile_json) const;
  static auto ParseDeviceProfile(const Json& jprofile) -> Device::Profile;

  DispatchQueue inactivity_check_queue_{"InactiveCheck", 1};
  std::atomic<bool> is_running_{false};

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

  std::unordered_map<ContactID, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;

  static constexpr std::chrono::seconds kPeerInactivityCheckInterval{30};
};

}  // namespace eillance
