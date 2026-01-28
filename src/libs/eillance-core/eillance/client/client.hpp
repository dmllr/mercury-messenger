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
#include "eillance/services/internal_messaging_service.hpp"
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

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

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

    media_call_service_ = std::make_shared<services::MediaCallService>(transport_);

    // Wire dependencies
    messaging_service_->SetContactRestoredCallback([this](ContactID const& id) {
      contact_service_->OnContactRestored(id);
    });
    contact_service_->SetMessagingService(messaging_service_.get());
    media_call_service_->SetMessagingService(messaging_service_.get());

    // Initialize Internal Messaging Service (Protocol Handler)
    internal_messaging_service_ = std::make_shared<services::InternalMessagingService>(
        transport_,
        account_service_,
        contact_service_,
        messaging_service_,
        media_call_service_,
        file_transfer_service_
    );

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

    // Delegate setup to Internal Messaging Service
    internal_messaging_service_->SetupPeerHandlers(peer, peer_id);

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
    if (status_callback_) {
      (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
    }
    // internal_messaging_service now handles login_success logic (starting delivery service)
    internal_messaging_service_->ProcessSystemMessage("login_success", {});
  }

  void ProcessPeerMessage(Json message) override {
    auto const from_it = message.find("from");
    if (from_it == message.end()) return;

    ContactID const peer_id(from_it->get<std::string>());
    auto const type_it = message.find("type");
    if (type_it == message.end()) return;

    internal_messaging_service_->ProcessSignalingMessage(peer_id, type_it->get<std::string>(), message);
  }

  void OnSystemMessage(std::string const& type, Json const& message) override {
    internal_messaging_service_->ProcessSystemMessage(type, message);
  }

  void OnDataChannelOpen(ContactID const& peer_id) override {
    internal_messaging_service_->OnDataChannelOpen(peer_id);
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

  void BroadcastProfileUpdate() {
    contact_service_->MarkAllContactsProfileSyncNeeded();
    // Trigger work check for all contacts
    auto const contacts = contact_service_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
        contact_service_->SyncProfile(contact.peer_id);
        Connect(contact.peer_id);
      }
    }
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::shared_ptr<services::InternalMessagingService> internal_messaging_service_;

  DispatchQueue background_queue_{"ClientBgQueue", 1};
  std::atomic<bool> is_running_{false};

  std::shared_ptr<StatusCallbackFunction> status_callback_;

  static constexpr std::chrono::seconds kPeerInactivityCheckInterval{30};
};

}  // namespace eillance
