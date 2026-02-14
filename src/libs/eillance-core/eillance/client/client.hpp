#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "eillance/events/service_connection_state.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/internal_messaging_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/invariant.hpp"
#include "eillance/view_model/view_model_defines.hpp"

namespace eillance {

class Client final : public transport::TransportDelegate {
 public:
  using Json = nlohmann::json;
  using FatalErrorCallback = std::function<void()>;

  Client(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> data_transport,
      std::shared_ptr<transport::MediaTransport> media_transport,
      FatalErrorCallback on_fatal_error = nullptr
  )
      : storage_(std::move(storage)),
        data_transport_(std::move(data_transport)),
        media_transport_(std::move(media_transport)),
        on_fatal_error_(std::move(on_fatal_error)) {
    data_transport_->SetDelegate(this);

    account_service_ = std::make_shared<services::AccountService>(storage_, data_transport_);
    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, data_transport_);

    messaging_service_ = std::make_shared<services::MessagingService>(
        storage_,
        data_transport_,
        contact_manager,
        file_transfer_service_,
        [this](ContactID const& id) { this->Connect(id); }
    );

    profile_service_ = std::make_shared<services::ProfileService>(
        storage_,
        data_transport_,
        contact_manager,
        account_service_,
        messaging_service_,
        [this](ContactID const& id) { this->Connect(id); }
    );

    contact_service_ = std::make_shared<services::ContactService>(
        storage_,
        data_transport_,
        contact_manager,
        account_service_,
        profile_service_,
        [this](ContactID const& id) { this->Connect(id); }
    );

    media_call_service_ =
        std::make_shared<services::MediaCallService>(storage_, data_transport_, media_transport_, account_service_);

    messaging_service_->SetContactRestoredCallback([this](ContactID const& id) {
      contact_service_->OnContactRestored(id);
    });
    contact_service_->SetMessagingService(messaging_service_);
    media_call_service_->SetMessagingService(messaging_service_);
    media_call_service_->SetConnectionHandler([this](ContactID const& id) { this->Connect(id); });

    internal_messaging_service_ = std::make_shared<services::InternalMessagingService>(
        data_transport_,
        media_transport_,
        account_service_,
        contact_service_,
        messaging_service_,
        media_call_service_,
        file_transfer_service_,
        profile_service_
    );

    // Setup Data Connection Callbacks
    data_transport_->SetStatusCallback([this](ContactID const& peer_id, EventSource const source, int state_val) {
      if (source == EventSource::peerConnectionState) {
        auto const state = static_cast<transport::PeerConnectionState>(state_val);
        if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
            state == transport::PeerConnectionState::disconnected) {
          // If data connection dies, we mark the delivery attempt as finished
          messaging_service_->OnConnectionAttemptFinished(peer_id);
          data_transport_->Disconnect(peer_id);

          bool media_active = false;
          if (auto peer = data_transport_->GetPeer(peer_id)) {
            media_active = peer->has_media_peer_connection;
          }

          if (media_active) {
            LGI << "Data connection lost for " << peer_id.ToString() << " but media is active. Reconnecting data.";
            Connect(peer_id);
          } else {
            // Also close media if data dies
            media_call_service_->OnMediaPeerConnectionStateChange(peer_id, state);
            media_transport_->Disconnect(peer_id);
            data_transport_->RemovePeer(peer_id);
          }
        }
      } else if (source == EventSource::system) {
        auto const state = static_cast<ServiceConnectionState>(state_val);
        if (state == ServiceConnectionState::failure || state == ServiceConnectionState::noNetwork ||
            state == ServiceConnectionState::disconnected) {
          for (auto const& [id, peer] : data_transport_->GetPeers()) {
            auto const pc_state = data_transport_->GetConnectionState(id);
            if (pc_state == transport::PeerConnectionState::newConnection ||
                pc_state == transport::PeerConnectionState::connecting) {
              messaging_service_->OnConnectionAttemptFinished(id);
              data_transport_->Disconnect(id);
              media_transport_->Disconnect(id);
            }
          }
        }
      }

      INVARIANT(status_callback_);
      (*status_callback_)(peer_id, source, state_val);
    });

    // Setup Media Connection Callbacks
    media_transport_->SetStatusCallback([this](ContactID const& peer_id, EventSource const source, int state_val) {
      if (source == EventSource::mediaConnectionState) {
        auto const state = static_cast<transport::PeerConnectionState>(state_val);
        media_call_service_->OnMediaPeerConnectionStateChange(peer_id, state);
        if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed) {
          media_transport_->Disconnect(peer_id);
        }
      }
      INVARIANT(status_callback_);
      (*status_callback_)(peer_id, source, state_val);
    });

    internal_messaging_service_->SetServerIceConfigCallback([this](Json const& config) {
      server_ice_config_ = config;
      ReevaluateIceConfiguration();
    });
  }

  Client(Client const&) = delete;
  Client(Client&&) = delete;
  auto operator=(Client const&) -> Client& = delete;
  auto operator=(Client&&) -> Client& = delete;
  ~Client() override {
    Terminate();
  }

  [[nodiscard]] auto GetAccountService() const -> std::shared_ptr<services::AccountService> {
    return account_service_;
  }
  [[nodiscard]] auto GetContactService() const -> std::shared_ptr<services::ContactService> {
    return contact_service_;
  }
  [[nodiscard]] auto GetMessagingService() const -> std::shared_ptr<services::MessagingService> {
    return messaging_service_;
  }
  [[nodiscard]] auto GetMediaCallService() const -> std::shared_ptr<services::MediaCallService> {
    return media_call_service_;
  }
  [[nodiscard]] auto GetFileTransferService() const -> std::shared_ptr<services::FileTransferService> {
    return file_transfer_service_;
  }
  [[nodiscard]] auto GetProfileService() const -> std::shared_ptr<services::ProfileService> {
    return profile_service_;
  }
  [[nodiscard]] auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage> {
    return storage_;
  }

  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    media_call_service_->SetVideoCapture(std::move(impl));
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    media_call_service_->SetAudioCapture(std::move(impl));
  }

  void Start() {
    if (!account_service_->IdentityExists()) {
      LGE << "Client started without an identity.";
      return;
    }
    if (is_running_.exchange(true)) {
      return;
    }

    ReevaluateIceConfiguration();

    if (auto url = account_service_->GetSettings()->GetSignalingServerUrl()) {
      data_transport_->SetSignalingUrl(*url);
    }

    data_transport_->SetId(account_service_->GetId());
    data_transport_->Start();
    messaging_service_->StartDeliveryService();

    StartWatchdog();
    background_queue_.Dispatch([this] { CheckForInactivePeers(); });
  }

  void Terminate() {
    is_running_ = false;
    StopWatchdog();
    background_queue_.RemovePending();
    messaging_service_->StopDeliveryService();
    data_transport_->Terminate();
  }

  void Configure(Config const& config) const {
    media_call_service_->Configure(config);
  }

  [[nodiscard]] auto GetId() const -> ContactID {
    return account_service_->GetId();
  }
  [[nodiscard]] auto IdentityExists() const -> bool {
    return account_service_->IdentityExists();
  }
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
    return account_service_->GetMnemonic();
  }

  auto GenerateNewIdentity() -> std::string {
    auto mnemonic = account_service_->GenerateNewIdentity();
    BroadcastProfileUpdate();
    return mnemonic;
  }

  auto RestoreIdentity(std::string const& m) -> bool {
    if (account_service_->RestoreIdentity(m)) {
      BroadcastProfileUpdate();
      Start();
      return true;
    }
    Start();
    return false;
  }

  [[nodiscard]] auto CreateBackup(std::string const& p, std::string const& pw) const -> bool {
    return account_service_->CreateBackup(p, pw);
  }

  auto RestoreBackup(std::string const& p, std::string const& pw) -> bool {
    is_running_ = false;
    messaging_service_->StopDeliveryService();
    data_transport_->Terminate();

    bool const success = account_service_->RestoreBackup(p, pw);

    if (success) {
      contact_service_->ReloadCache();
      if (on_data_reloaded_) {
        on_data_reloaded_();
      }
      Start();
      BroadcastProfileUpdate();
      return true;
    }
    Start();
    return false;
  }

  void UpdateSignalingUrl(std::string const& url) const {
    account_service_->UpdateSignalingUrl(url);
  }

  void UpdateMyProfile(
      std::optional<std::string> const& n,
      std::optional<std::string> const& b,
      std::optional<std::string> const& p
  ) {
    account_service_->UpdateMyProfile(n, b, p);
    BroadcastProfileUpdate();
  }

  void
  UpdateIceConfig(std::string const& stun, std::string const& turn, std::string const& user, std::string const& pass) {
    account_service_->GetSettings()->SetCustomIceConfig(stun, turn, user, pass);
    ReevaluateIceConfiguration();
  }

  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager* {
    return account_service_->GetSettings();
  }

  auto AddContact(ContactID const& id, std::optional<std::vector<std::byte>> const& t) const -> bool {
    return contact_service_->AddContact(id, t);
  }
  auto GenerateContactToken() const -> std::vector<std::byte> {
    return contact_service_->GenerateContactToken();
  }
  void ConfirmContact(ContactID const& id) const {
    contact_service_->ConfirmContact(id);
  }
  void BlockContact(ContactID const& id, bool n) const {
    contact_service_->BlockContact(id, n);
  }
  void DeleteContact(ContactID const& id) const {
    contact_service_->DeleteContact(id);
  }
  void UnblockContact(ContactID const& id) const {
    contact_service_->UnblockContact(id);
  }
  void SetContactDisplayNameOverride(ContactID const& id, std::string const& n) const {
    contact_service_->SetContactDisplayNameOverride(id, n);
  }
  void ResetContactDisplayNameOverride(ContactID const& id) const {
    contact_service_->ResetContactDisplayNameOverride(id);
  }
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact> {
    return contact_service_->GetContacts();
  }

  auto SendChatMessage(ContactID const& id, std::string const& msg) const -> messaging::HistoryEntryRecord {
    return messaging_service_->SendChatMessage(id, msg);
  }
  void MarkMessagesAsRead(ContactID const& id, std::vector<std::uint64_t> const& mids) const {
    messaging_service_->MarkMessagesAsRead(id, mids);
  }
  [[nodiscard]] auto GetMessageHistoryPage(ContactID const& id, size_t const page_index, size_t const page_size) const
      -> messaging::HistoryPage {
    return messaging_service_->GetHistoryPage(id, page_index, page_size);
  }
  [[nodiscard]] auto GetFullMessageContent(ContactID const& id, uint64_t const msg_id) const
      -> std::optional<std::string> {
    return messaging_service_->GetFullMessageContent(id, msg_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& ids) const -> std::vector<messaging::ChatSummary> {
    return messaging_service_->GetChatSummaries(ids);
  }
  [[nodiscard]] auto GetUnreadMessages(ContactID const& id) const -> std::vector<messaging::HistoryEntryRecord> {
    return messaging_service_->GetUnreadMessages(id);
  }

  [[nodiscard]] auto SearchMessages(ContactID const& peer_id, std::string const& query) const
      -> std::vector<messaging::HistoryEntryRecord> {
    return messaging_service_->SearchMessages(peer_id, query);
  }

  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<messaging::HistoryEntryRecord> {
    return messaging_service_->SearchAllMessages(query);
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

  [[nodiscard]] auto GetAttachment(std::string const& attachment_id) const -> std::optional<messaging::Attachment> {
    return storage_->GetAttachment(attachment_id);
  }

  void Connect(ContactID const& peer_id) const {
    auto const peer = data_transport_->GetPeer(peer_id);
    if (peer && peer->data_channel_is_open) {
      messaging_service_->SendQueuedMessages(peer_id);
      return;
    }

    auto const state = data_transport_->GetConnectionState(peer_id);
    if (state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection) {
      return;
    }

    if (!peer) {
      auto const new_peer = std::make_shared<transport::Peer>(peer_id);
      data_transport_->AddPeer(peer_id, new_peer);
      internal_messaging_service_->SetupPeerHandlers(new_peer, peer_id);
    } else if (!peer->on_data_channel_message) {
      internal_messaging_service_->SetupPeerHandlers(peer, peer_id);
    }

    data_transport_->Connect(peer_id);
    data_transport_->CreateOffer(peer_id);
  }

  void StartVoiceCall(ContactID const& id) const {
    media_call_service_->StartVoiceCall(id);
  }
  void StartVideoCall(ContactID const& id) const {
    media_call_service_->StartVideoCall(id);
  }
  void AcceptCall(ContactID const& id) const {
    media_call_service_->AcceptCall(id);
  }
  void RejectCall(ContactID const& id) const {
    media_call_service_->RejectCall(id);
  }
  void Hangup(ContactID const& id, bool n) const {
    media_call_service_->Hangup(id, n);
  }

  [[nodiscard]] auto GetCurrentCallState() const -> view_model::CallState {
    auto session = media_call_service_->GetCurrentSession();
    if (!session) return {};

    return {
        .is_connecting = !session->is_active && !session->is_ringing,
        .is_in_call = session->is_active,
        .is_ringing = session->is_ringing,
        .call_direction = session->is_outgoing ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
        .peer_id = session->peer_id,
        .call_type = session->is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall
    };
  }

  [[nodiscard]] auto GetPeerConnectionState(ContactID const& id) const -> transport::PeerConnectionState {
    return data_transport_->GetConnectionState(id);
  }

  void DebugEraseAllHistory() const {
    storage_->DebugClearAllHistory();
  }
  void DebugEraseAllContacts() const {
    storage_->DebugClearAllContacts();
    contact_service_->DebugClearCache();
    storage_->DebugClearAllHistory();
  }
  void DebugRevokeAllContactApprovals() const {
    storage_->DebugRevokeAllContactApprovals();
    contact_service_->ReloadCache();
  }

  [[nodiscard]] auto GetAllPendingActions() const -> std::vector<view_model::DebugPendingAction> {
    std::vector<view_model::DebugPendingAction> result;
    auto contacts = contact_service_->GetContacts();
    for (auto const& contact : contacts) {
      auto actions = storage_->GetPendingActions(contact.peer_id);
      for (auto const& action : actions) {
        result.push_back({action.peer_id, action.id, action.payload});
      }
    }
    return result;
  }

  [[nodiscard]] auto GetConnectionDebugInfo() const -> std::vector<view_model::DebugConnectionInfo> {
    std::vector<view_model::DebugConnectionInfo> result;
    auto now = Time::Now();
    auto peers = data_transport_->GetPeers();

    auto contacts = contact_service_->GetContacts();
    for (auto const& contact : contacts) {
      if (!peers.contains(contact.peer_id)) {
        result.push_back({contact.peer_id, transport::PeerConnectionState::disconnected, -1, false, false});
      }
    }

    for (auto const& [id, peer] : peers) {
      std::erase_if(result, [&](auto const& item) { return item.peer_id == id; });

      int64_t const ago = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::microseconds(now - peer->last_activity_time.load())
      )
                              .count();

      view_model::DebugConnectionInfo const info{
          id,
          data_transport_->GetConnectionState(id),
          ago,
          peer->data_channel_is_open.load(),
          peer->has_media_peer_connection.load()
      };

      result.push_back(info);
    }
    return result;
  }

  [[nodiscard]] auto GetAllUnreadInfo() const -> std::vector<view_model::DebugUnreadInfo> {
    std::vector<view_model::DebugUnreadInfo> result;
    auto contacts = contact_service_->GetContacts();
    std::vector<ContactID> ids;
    for (auto const& c : contacts) ids.push_back(c.peer_id);

    auto summaries = messaging_service_->GetChatSummaries(ids);
    for (auto const& s : summaries) {
      if (s.unread_count > 0) result.push_back({s.peer_id, s.unread_count, s.last_message.timestamp});
    }
    return result;
  }

  using StatusCallbackFunction = transport::DataTransport::StatusCallbackFunction;
  void SetStatusCallback(StatusCallbackFunction const& cb) {
    status_callback_ = std::make_shared<StatusCallbackFunction>(cb);
  }

  using OnDataReloadedCallback = std::function<void()>;
  void SetDataReloadedCallback(OnDataReloadedCallback cb) {
    on_data_reloaded_ = std::move(cb);
  }

 private:
  void OnPeerReady(std::shared_ptr<transport::Peer> const&) override {
  }

  void OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) override {
    if (error == "peer_not_found") {
      LGW << "Connection attempt failed for peer " << peer_id.ToString() << " (not online).";
      messaging_service_->OnConnectionAttemptFinished(peer_id);
      if (data_transport_->GetPeer(peer_id)) {
        // Disconnect media first to clear the has_media_peer_connection flag on the shared peer.
        // This prevents the data transport disconnection callback from triggering an immediate
        // reconnect loop thinking media is still active.
        media_transport_->Disconnect(peer_id);
        data_transport_->Disconnect(peer_id);
      }
    }
  }

  auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> override {
    return account_service_->Sign(message);
  }

  void OnLoginSuccess() override {
    INVARIANT(status_callback_);
    (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(ServiceConnectionState::online));
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
    media_call_service_->OnDataChannelOpen(peer_id);
  }

  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) override {
    media_call_service_->OnFrameReceived(peer_id, frame);
  }

  void CheckForInactivePeers() {
    if (!is_running_) return;
    auto const now = Time::Now();
    auto const timeout = std::chrono::duration_cast<std::chrono::microseconds>(
                             account_service_->GetSettings()->GetPeerInactivityTimeout()
    )
                             .count();

    for (auto const& [id, peer] : data_transport_->GetPeers()) {
      bool const is_data_connected =
          data_transport_->GetConnectionState(id) == transport::PeerConnectionState::connected;

      if (peer->has_media_peer_connection) {
        peer->last_activity_time = now;
        if (!peer->data_channel_is_open) {
          Connect(id);
        }
      } else if (is_data_connected) {
        if ((now - peer->last_activity_time.load()) > static_cast<uint64_t>(timeout)) {
          LGI << "Peer " << id.ToString() << " inactive. Closing.";
          data_transport_->Disconnect(id);
        }
      }
    }
    background_queue_.DispatchAfter(peerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
  }

  void StartWatchdog() {
    last_heartbeat_ = Time::Now();
    watchdog_running_ = true;
    watchdog_thread_ = std::thread([this] {
      while (watchdog_running_) {
        {
          std::unique_lock<std::mutex> lock(watchdog_mutex_);
          if (watchdog_cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !watchdog_running_; })) {
            break;
          }
        }
        auto const now = Time::Now();
        background_queue_.Dispatch([this] { last_heartbeat_ = Time::Now(); });
        if (now > last_heartbeat_.load() + 15000000) {
          LGE << "Core Watchdog: Main queue stalled. Triggering fatal error.";
          if (on_fatal_error_) on_fatal_error_();
          std::unique_lock<std::mutex> lock(watchdog_mutex_);
          if (watchdog_cv_.wait_for(lock, std::chrono::seconds(10), [this] { return !watchdog_running_; })) {
            break;
          }
        }
      }
    });
  }

  void StopWatchdog() {
    watchdog_running_ = false;
    watchdog_cv_.notify_all();
    if (watchdog_thread_.joinable()) watchdog_thread_.join();
  }

  void BroadcastProfileUpdate() {
    profile_service_->MarkAllContactsProfileSyncNeeded();
    auto const contacts = contact_service_->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
        profile_service_->SyncProfile(contact.peer_id);
        Connect(contact.peer_id);
      }
    }
  }

  void ReevaluateIceConfiguration() {
    Json final_config = Json::array();
    auto* settings = account_service_->GetSettings();
    if (auto stun = settings->GetCustomStun(); stun && !stun->empty()) {
      final_config.push_back({{"urls", std::vector<std::string>{*stun}}});
    }
    if (auto turn = settings->GetCustomTurn(); turn && !turn->empty()) {
      Json turn_server;
      turn_server["urls"] = std::vector<std::string>{*turn};
      if (auto user = settings->GetCustomTurnUser(); user && !user->empty()) {
        turn_server["username"] = *user;
      }
      if (auto pass = settings->GetCustomTurnPass(); pass && !pass->empty()) {
        turn_server["credential"] = *pass;
      }
      final_config.push_back(turn_server);
    }
    if (final_config.empty() && !server_ice_config_.empty()) {
      final_config = server_ice_config_;
    }
    if (!final_config.empty()) {
      auto json_str = final_config.dump();
      data_transport_->SetIceServers(json_str);
      media_transport_->SetIceServers(json_str);
    }
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;

  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::shared_ptr<services::ProfileService> profile_service_;
  std::shared_ptr<services::InternalMessagingService> internal_messaging_service_;

  DispatchQueue background_queue_{"ClientBgQueue", 1};
  std::atomic<bool> is_running_{false};

  std::shared_ptr<StatusCallbackFunction> status_callback_;
  OnDataReloadedCallback on_data_reloaded_;

  std::thread watchdog_thread_;
  std::atomic<bool> watchdog_running_{false};
  std::condition_variable watchdog_cv_;
  std::mutex watchdog_mutex_;
  std::atomic<uint64_t> last_heartbeat_{0};
  FatalErrorCallback on_fatal_error_;

  Json server_ice_config_{Json::array()};

  static constexpr std::chrono::seconds peerInactivityCheckInterval{20};
};

}  // namespace eillance
