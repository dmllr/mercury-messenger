#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/events/service_connection_state.hpp"
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
  using OnFrameReceivedCallback = std::function<void(std::vector<std::byte> const&)>;
  using OnProfileReceivedCallback = std::function<void(ContactID const&, Device::Profile const&)>;
  using OnChatMessageReceivedCallback = std::function<void(ContactID const&, messaging::HistoryEntryRecord const&)>;
  using OnContactRequestCallback = std::function<void(ContactID const& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(ContactID const& peer_id, identity::ContactStatus status)>;
  using OnMessageStatusChangedCallback =
      std::function<void(ContactID const& peer_id, std::uint64_t message_id, messaging::MessageStatus status)>;
  using OnPeerListUpdateCallback = std::function<void(std::vector<Device> const&)>;
  using StatusCallbackFunction = transport::Transport::StatusCallbackFunction;
  using OnCallRingingCallback = std::function<void(ContactID const& peer_id)>;
  using OnCallAcceptedCallback = std::function<void(ContactID const& peer_id)>;
  using OnIncomingCallCallback = std::function<void(ContactID const& peer_id, messaging::HistoryEntryKind call_type)>;
  using OnMyProfileUpdatedCallback = std::function<void()>;
  using OnContactProfileUpdatedCallback = std::function<void(ContactID const& peer_id)>;
  using OnCallTerminatedCallback = std::function<void(ContactID const& peer_id)>;
  using OnContactRemovedCallback = std::function<void(ContactID const& peer_id)>;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport);

  Client(Client&&) = delete;
  Client(Client const&) = delete;
  auto operator=(Client&&) -> Client& = delete;
  auto operator=(Client const&) -> Client& = delete;
  ~Client() override;

  // Setter injection for platform-specific capture implementations
  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    video_capture_ = std::make_unique<Capture>(std::move(impl));
    if (audio_capture_) {
      InitializeAvStream();
    }
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    audio_capture_ = std::make_unique<Capture>(std::move(impl));
    if (video_capture_) {
      InitializeAvStream();
    }
  }

  [[nodiscard]] auto GetId() const -> ContactID;
  [[nodiscard]] auto IdentityExists() const -> bool;
  auto GenerateNewIdentity() const -> std::string;
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string>;
  auto RestoreIdentity(std::string const& mnemonic) const -> bool;

  auto CreateBackup(std::string const& output_path, std::string const& password) const -> bool;
  auto RestoreBackup(std::string const& input_path, std::string const& password) -> bool;

  [[nodiscard]] auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage>;
  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager*;
  void SetName(std::string const& name) const;
  void Start();
  void Terminate();
  void SetStatusCallback(StatusCallbackFunction const& callback);

  void SetFrameReceivedCallback(OnFrameReceivedCallback const& callback);
  void SetProfileReceivedCallback(OnProfileReceivedCallback const& callback);
  void SetChatMessageReceivedCallback(OnChatMessageReceivedCallback const& callback);
  void SetContactRequestCallback(OnContactRequestCallback const& callback);
  void SetContactStatusChangedCallback(OnContactStatusChangedCallback const& callback);
  void SetMessageStatusChangedCallback(OnMessageStatusChangedCallback const& callback);
  void SetPeerListUpdateCallback(OnPeerListUpdateCallback const& callback);
  void SetIncomingCallCallback(OnIncomingCallCallback const& callback);
  void SetOnCallRingingCallback(OnCallRingingCallback const& callback);
  void SetOnCallAcceptedCallback(OnCallAcceptedCallback const& callback);
  void SetOnCallTerminatedCallback(OnCallTerminatedCallback const& callback);
  void SetOnMyProfileUpdatedCallback(OnMyProfileUpdatedCallback const& callback);
  void SetOnContactRemovedCallback(OnContactRemovedCallback const& callback);
  void SetOnContactProfileUpdatedCallback(OnContactProfileUpdatedCallback const& callback);

  void Call(ContactID const& peer_id);
  void Connect(ContactID const& peer_id);
  void StartVoiceCall(ContactID const& peer_id);
  void StartVideoCall(ContactID const& peer_id);
  void AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind) const;
  void RejectCall(ContactID const& peer_id) const;
  void AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) const;
  auto GenerateContactToken() -> std::vector<std::byte>;
  void ConfirmContact(ContactID const& peer_id) const;
  void BlockContact(ContactID const& peer_id, bool notify_peer) const;
  void DeleteContact(ContactID const& peer_id) const;
  void UnblockContact(ContactID const& peer_id) const;
  [[nodiscard]] auto GetPeerConnectionState(ContactID const& peer_id) const -> eillance::transport::PeerConnectionState;
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact>;
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids
  ) const -> std::vector<messaging::ChatSummary>;
  void Hangup(ContactID const& peer_id, bool notify_peer) const;
  void Configure(Config const& config) const;
  auto SendChatMessage(ContactID const& peer_id, std::string const& message) -> messaging::HistoryEntryRecord;
  void UpdateSignalingUrl(std::string const& url) const;
  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) const;
  [[nodiscard]] auto GetMessageHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
      -> messaging::HistoryPage;
  [[nodiscard]] auto GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string>;
  void UpdateMyProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  );
  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& override_name) const;
  void ResetContactDisplayNameOverride(ContactID const& peer_id) const;

  // Debug methods
  void DebugEraseAllHistory() const;
  void DebugEraseAllContacts() const;
  void DebugRevokeAllApprovals() const;

  // TransportDelegate implementation
  void OnPeerReady(std::shared_ptr<Peer> const& peer) override;
  void OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) override;
  auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> override;
  void OnLoginSuccess() override;
  void ProcessPeerMessage(Json message) override;
  void OnSystemMessage(std::string const& type, Json const& message) override;
  void OnDataChannelOpen(ContactID const& peer_id) override;
  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) override;

 private:
  void InitializeAvStream();
  void StartMediaCall(ContactID const& peer_id, bool is_video);
  void UpdateAndNotifyPeerList();
  void SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type type, std::uint64_t sample_time)
      const;
  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message);
  void SendQueuedMessages(ContactID const& peer_id);

  void CheckForInactivePeers();
  void UpdatePeerActivity(ContactID const& peer_id) const;

  void SetupDataChannel(std::shared_ptr<Peer> const& peer, ContactID const& peer_id);
  void HandleProfileUpdate(ContactID const& peer_id, Json const& profile_json) const;
  static auto ParseDeviceProfile(Json const& jprofile) -> Device::Profile;

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

  static constexpr std::chrono::seconds peerInactivityCheckInterval{30};
};

}  // namespace eillance
