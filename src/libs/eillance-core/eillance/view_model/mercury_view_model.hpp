#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::view_model {

struct UIState {
  struct CallState {
    bool is_connecting = false;
    bool is_in_call = false;
    bool is_ringing = false;
    messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
    ContactID peer_id;
    messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
  };

  bool identity_exists = true;

  struct ViewContact {
    ContactID peer_id;
    std::string display_name;
    std::optional<std::string> bio;
    std::optional<std::string> photo;  // base64
    identity::ContactStatus status;
    eillance::transport::PeerConnectionState connection_state = eillance::transport::PeerConnectionState::Disconnected;
    uint32_t unread_count = 0;
    std::optional<std::string> last_message_snippet;
    uint64_t last_activity_timestamp = 0;
  };

  struct MyProfile {
    ContactID my_id;
    bool identity_exists = true;
    std::optional<std::string> my_display_name;
    std::optional<std::string> my_bio;
    std::optional<std::string> my_photo;  // base64
  };

  MyProfile my_profile;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<ViewContact> contacts;
  std::optional<ContactID> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages = false;
  CallState call_state;
  std::optional<std::string> signaling_server_url;
  int peer_inactivity_timeout_seconds = 120;
};

enum class StateChange {
  MyProfile,
  Contacts,
  Connection,
  SelectedContact,
  InteractionHistory,
  CallState,
  SignalingUrl,
  PeerInactivityTimeout
};

class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<Client> client);
  ~MercuryViewModel() = default;

  MercuryViewModel(const MercuryViewModel&) = delete;
  auto operator=(const MercuryViewModel&) -> MercuryViewModel& = delete;
  MercuryViewModel(MercuryViewModel&&) = delete;
  auto operator=(MercuryViewModel&&) -> MercuryViewModel& = delete;

  auto GenerateNewIdentity() -> std::string;
  auto RestoreIdentity(const std::string& mnemonic) -> bool;
  auto GetMnemonic() -> std::optional<std::string>;

  auto CreateBackup(const std::string& path, const std::string& password) -> bool;
  auto RestoreBackup(const std::string& path, const std::string& password) -> bool;

  void SelectContact(const ContactID& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void StartVoiceCall();
  void StartVideoCall();
  void Hangup();
  void AcceptCall();
  void RejectCall();
  void BlockContact(const ContactID& peer_id);
  void DeleteContact(const ContactID& peer_id);
  void UnblockContact(const ContactID& peer_id);
  void AddContact(const ContactID& peer_id, const std::optional<std::string>& token = std::nullopt);
  auto GenerateContactToken() -> std::string;
  void UpdateSignalingUrl(const std::string& url);
  void SetPeerInactivityTimeout(int seconds);
  void AcceptContact(const ContactID& peer_id);
  void UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo);
  void SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name);
  void ResetContactDisplayNameOverride(const ContactID& peer_id);
  void LoadOlderMessages();
  void LoadFullMessage(uint64_t message_id);
  void RefreshState();
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);
  void AddSystemMessageToChat(const std::string& message);
  void SelectContactByIndex(size_t index);
  void DebugEraseAllHistory();
  void DebugEraseAllContacts();
  void DebugRevokeAllApprovals();

  [[nodiscard]] auto GetState() const -> UIState;

  // --- Granular Getters for Android UI ---
  [[nodiscard]] auto GetMyProfile() const -> UIState::MyProfile;
  [[nodiscard]] auto GetContacts() const -> std::vector<UIState::ViewContact>;
  [[nodiscard]] auto GetInteractionHistory() const -> std::deque<messaging::HistoryEntryRecord>;
  [[nodiscard]] auto HasOlderMessages() const -> bool;
  [[nodiscard]] auto GetCallState() const -> UIState::CallState;

  void SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback);

private:
  void SetupClientCallbacks();
  void NotifyStateChanged(const std::vector<StateChange>& changes) const;
  void UpdateContacts(bool notify);
  void UpdateMyProfileFromSettings(bool notify);
  void UpdateChats(bool notify);
  void LoadInitialChatHistory(const ContactID& peer_id);

  static inline std::atomic<std::uint64_t> nextSystemMessageId{10000000000000000000ULL};
  std::shared_ptr<Client> client_;

  mutable std::mutex state_mutex_;

  UIState::MyProfile my_profile_;
  ServiceConnectionState connection_state_ = ServiceConnectionState::unknown;
  std::vector<UIState::ViewContact> contacts_;
  std::optional<ContactID> selected_contact_id_;
  std::deque<messaging::HistoryEntryRecord> interaction_history_;
  bool has_older_messages_ = false;
  UIState::CallState call_state_;
  std::optional<std::string> signaling_server_url_;
  int peer_inactivity_timeout_seconds_ = 120;

  std::unordered_map<ContactID, size_t> chat_pages_loaded_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};

  static constexpr size_t kChatPageSize = 50;

  std::function<void(const std::vector<StateChange>&)> on_state_changed_callback_;
};

}  // namespace eillance::view_model
