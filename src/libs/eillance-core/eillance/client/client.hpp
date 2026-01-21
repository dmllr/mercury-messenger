#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "eillance/client/device.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance {

class Client final : public transport::TransportDelegate {
 public:
  using Json = nlohmann::json;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, std::shared_ptr<transport::Transport> transport);

  Client(Client const&) = delete;
  Client(Client&&) = delete;
  auto operator=(Client const&) -> Client& = delete;
  auto operator=(Client&&) -> Client& = delete;
  ~Client() override;

  // --- Service Accessors ---
  [[nodiscard]] auto GetAccountService() const -> std::shared_ptr<services::AccountService> { return account_service_; }
  [[nodiscard]] auto GetContactService() const -> std::shared_ptr<services::ContactService> { return contact_service_; }
  [[nodiscard]] auto GetMessagingService() const -> std::shared_ptr<services::MessagingService> {
    return messaging_service_;
  }
  [[nodiscard]] auto GetMediaCallService() const -> std::shared_ptr<services::MediaCallService> {
    return media_call_service_;
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
  void Start();
  void Terminate();
  void Configure(Config const& config) const;

  // --- Facade Methods (Delegates to Services for ViewModel compatibility) ---

  // Identity / Account
  [[nodiscard]] auto GetId() const -> ContactID;
  [[nodiscard]] auto IdentityExists() const -> bool;
  auto GenerateNewIdentity() const -> std::string;
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string>;
  auto RestoreIdentity(std::string const& mnemonic) const -> bool;
  auto CreateBackup(std::string const& output_path, std::string const& password) const -> bool;
  auto RestoreBackup(std::string const& input_path, std::string const& password) -> bool;
  void SetName(std::string const& name) const;
  void UpdateSignalingUrl(std::string const& url) const;
  void UpdateMyProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  );
  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager*;

  // Contacts
  void AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) const;
  auto GenerateContactToken() -> std::vector<std::byte>;
  void ConfirmContact(ContactID const& peer_id) const;
  void BlockContact(ContactID const& peer_id, bool notify_peer) const;
  void DeleteContact(ContactID const& peer_id) const;
  void UnblockContact(ContactID const& peer_id) const;
  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& override_name) const;
  void ResetContactDisplayNameOverride(ContactID const& peer_id) const;
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact>;

  // Messaging
  auto SendChatMessage(ContactID const& peer_id, std::string const& message) -> messaging::HistoryEntryRecord;
  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) const;
  [[nodiscard]] auto GetMessageHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
      -> messaging::HistoryPage;
  [[nodiscard]] auto GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string>;
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids
  ) const -> std::vector<messaging::ChatSummary>;

  // Call / Connection
  void Call(ContactID const& peer_id);  // Connect data channel
  void Connect(ContactID const& peer_id);
  void StartVoiceCall(ContactID const& peer_id);
  void StartVideoCall(ContactID const& peer_id);
  void AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind) const;
  void RejectCall(ContactID const& peer_id) const;
  void Hangup(ContactID const& peer_id, bool notify_peer) const;
  [[nodiscard]] auto GetPeerConnectionState(ContactID const& peer_id) const -> transport::PeerConnectionState;

  // Debug
  void DebugEraseAllHistory() const;
  void DebugEraseAllContacts() const;
  void DebugRevokeAllApprovals() const;

  // Callbacks (Deprecated access, prefer routing via ClientEventRouter)
  using StatusCallbackFunction = transport::Transport::StatusCallbackFunction;
  void SetStatusCallback(StatusCallbackFunction const& callback);

 private:
  // TransportDelegate Implementation
  void OnPeerReady(std::shared_ptr<transport::Peer> const& peer) override;
  void OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) override;
  auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> override;
  void OnLoginSuccess() override;
  void ProcessPeerMessage(Json message) override;
  void OnSystemMessage(std::string const& type, Json const& message) override;
  void OnDataChannelOpen(ContactID const& peer_id) override;
  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) override;

  // Internal Logic
  void CheckForInactivePeers();
  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message);
  void SetupPeerHandlers(std::shared_ptr<transport::Peer> const& peer, ContactID const& peer_id);

  // Helpers
  void HandleProfileInfo(ContactID const& peer_id, Json const& message);
  void SendProfileInfo(ContactID const& peer_id);

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;

  DispatchQueue background_queue_{"ClientBgQueue", 1};
  std::atomic<bool> is_running_{false};

  // Keep legacy status callback for connectivity reporting
  std::shared_ptr<StatusCallbackFunction> status_callback_;

  static constexpr std::chrono::seconds kPeerInactivityCheckInterval{30};
};

}  // namespace eillance
