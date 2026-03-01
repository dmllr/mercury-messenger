#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/internal_messaging_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"
#include "eillance/view_model/view_model_defines.hpp"

namespace eillance {

class Client final {
 public:
  using Json = nlohmann::json;

  Client(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> data_transport,
      std::shared_ptr<transport::MediaTransport> media_transport
  ) {
    sm_ = std::make_unique<services::ServiceManager>();
    sm_->storage = std::move(storage);
    sm_->data_transport = std::move(data_transport);
    sm_->media_transport = std::move(media_transport);
    sm_->contact_manager = std::make_shared<identity::ContactManager>(sm_->storage);

    sm_->account = std::make_shared<services::AccountService>(sm_.get());
    sm_->file_transfer = std::make_shared<services::FileTransferService>(sm_.get());
    sm_->messaging = std::make_shared<services::MessagingService>(sm_.get());
    sm_->profile = std::make_shared<services::ProfileService>(sm_.get());
    sm_->contact = std::make_shared<services::ContactService>(sm_.get());
    sm_->media_call = std::make_shared<services::MediaCallService>(sm_.get());
    sm_->internal_messaging = std::make_shared<services::InternalMessagingService>(sm_.get());

    // Core subscriptions for transport-level handling that were formerly inside callbacks
    auto sub1 = EventQueue::Instance().Subscribe<EventStatusChanged>([this](auto const& event) {
      if (event.source == EventSource::peerConnectionState) {
        auto const state = static_cast<transport::PeerConnectionState>(event.state);
        if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
            state == transport::PeerConnectionState::disconnected) {
          EventQueue::Instance().Push(EventConnectionAttemptFinished{event.peer_id});
          sm_->data_transport->Disconnect(event.peer_id);

          bool media_active = false;
          if (auto peer = sm_->data_transport->GetPeer(event.peer_id)) {
            media_active = peer->has_media_peer_connection;
          }

          if (media_active) {
            LGI << "Data connection lost for " << event.peer_id.ToString()
                << " but media is active. Reconnecting data.";
            Connect(event.peer_id);
          } else {
            EventQueue::Instance().Push(EventMediaPeerConnectionStateChanged{event.peer_id, state});
            sm_->media_transport->Disconnect(event.peer_id);
            sm_->data_transport->RemovePeer(event.peer_id);
          }
        }
      } else if (event.source == EventSource::system) {
        auto const state = static_cast<ServiceConnectionState>(event.state);
        if (state == ServiceConnectionState::failure || state == ServiceConnectionState::noNetwork ||
            state == ServiceConnectionState::disconnected) {
          EventQueue::Instance().Push(EventStopDeliveryService{});
          for (auto const& [id, peer] : sm_->data_transport->GetPeers()) {
            auto const pc_state = sm_->data_transport->GetConnectionState(id);
            if (pc_state == transport::PeerConnectionState::newConnection ||
                pc_state == transport::PeerConnectionState::connecting) {
              EventQueue::Instance().Push(EventConnectionAttemptFinished{id});
              sm_->data_transport->Disconnect(id);
              sm_->media_transport->Disconnect(id);
            }
          }
        } else if (state == ServiceConnectionState::online) {
          if (this->is_running_.load()) {
            sm_->messaging->StartDeliveryService();
          }
        }
      } else if (event.source == EventSource::mediaConnectionState) {
        auto const state = static_cast<transport::PeerConnectionState>(event.state);
        EventQueue::Instance().Push(EventMediaPeerConnectionStateChanged{event.peer_id, state});
        if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed) {
          sm_->media_transport->Disconnect(event.peer_id);
        }
      } else if (event.source == EventSource::gatheringState) {
        EventQueue::Instance().Push(EventMediaGatheringStateChanged{event.peer_id, event.state});
      }
    });
    event_unsubs_.push_back([sub1]() { EventQueue::Instance().Unsubscribe<EventStatusChanged>(sub1); });

    auto sub3 =
        EventQueue::Instance().Subscribe<EventConnectionRequested>([this](auto const& e) { this->Connect(e.peer_id); });
    event_unsubs_.push_back([sub3]() { EventQueue::Instance().Unsubscribe<EventConnectionRequested>(sub3); });

    auto sub4 = EventQueue::Instance().Subscribe<EventServerIceConfig>([this](auto const& e) {
      server_ice_config_ = nlohmann::json::parse(e.config_json);
      ReevaluateIceConfiguration();
    });
    event_unsubs_.push_back([sub4]() { EventQueue::Instance().Unsubscribe<EventServerIceConfig>(sub4); });

    auto sub_sig_err = EventQueue::Instance().Subscribe<EventSignalingError>([this](auto const& e) {
      if (e.error == "peer_not_found") {
        LGW << "Connection attempt failed for peer " << e.peer_id.ToString() << " (not online).";
        EventQueue::Instance().Push(EventConnectionAttemptFinished{e.peer_id});
        if (sm_->data_transport->GetPeer(e.peer_id)) {
          sm_->media_transport->Disconnect(e.peer_id);
          sm_->data_transport->Disconnect(e.peer_id);
        }
      }
    });
    event_unsubs_.push_back([sub_sig_err]() { EventQueue::Instance().Unsubscribe<EventSignalingError>(sub_sig_err); });
  }

  Client(Client const&) = delete;
  Client(Client&&) = delete;
  auto operator=(Client const&) -> Client& = delete;
  auto operator=(Client&&) -> Client& = delete;
  ~Client() {
    for (auto const& unsub : event_unsubs_) unsub();
    Terminate();
  }

  // Backwards compatibility for Android/CLI
  [[nodiscard]] auto Account() const -> std::shared_ptr<services::AccountService> {
    return sm_->account;
  }
  [[nodiscard]] auto Contacts() const -> std::shared_ptr<services::ContactService> {
    return sm_->contact;
  }
  [[nodiscard]] auto Messaging() const -> std::shared_ptr<services::MessagingService> {
    return sm_->messaging;
  }
  [[nodiscard]] auto Calls() const -> std::shared_ptr<services::MediaCallService> {
    return sm_->media_call;
  }
  [[nodiscard]] auto Attachments() const -> std::shared_ptr<services::FileTransferService> {
    return sm_->file_transfer;
  }
  [[nodiscard]] auto Profile() const -> std::shared_ptr<services::ProfileService> {
    return sm_->profile;
  }

  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    sm_->media_call->SetVideoCapture(std::move(impl));
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    sm_->media_call->SetAudioCapture(std::move(impl));
  }

  void Start() {
    if (!sm_->account->IdentityExists()) {
      LGE << "Client started without an identity.";
      return;
    }
    if (is_running_.exchange(true)) {
      return;
    }
    ReevaluateIceConfiguration();
    if (auto url = sm_->account->GetSettings()->GetSignalingServerUrl()) {
      sm_->data_transport->SetSignalingUrl(*url);
    }
    sm_->data_transport->SetId(sm_->account->GetId());
    sm_->data_transport->Start();
    sm_->messaging->StartDeliveryService();
    StartWatchdog();
    background_queue_.Dispatch([this] { CheckForInactivePeers(); });
  }

  void Terminate() {
    is_running_ = false;
    StopWatchdog();
    background_queue_.RemovePending();
    EventQueue::Instance().Push(EventStopDeliveryService{});

    auto peers = sm_->data_transport->GetPeers();
    for (auto const& [id, peer] : peers) {
      sm_->media_transport->Disconnect(id);
      sm_->data_transport->Disconnect(id);
      sm_->data_transport->RemovePeer(id);
    }
    sm_->data_transport->Terminate();
  }

  void Configure(Config const& config) const {
    sm_->media_call->Configure(config);
  }

  [[nodiscard]] auto GetId() const -> ContactID {
    return sm_->account->GetId();
  }
  [[nodiscard]] auto IdentityExists() const -> bool {
    return sm_->account->IdentityExists();
  }
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
    return sm_->account->GetMnemonic();
  }

  auto GenerateNewIdentity() -> std::string {
    auto mnemonic = sm_->account->GenerateNewIdentity();
    BroadcastProfileUpdate();
    return mnemonic;
  }

  auto RestoreIdentity(std::string const& m) -> bool {
    if (sm_->account->RestoreIdentity(m)) {
      BroadcastProfileUpdate();
      Start();
      return true;
    }
    Start();
    return false;
  }

  [[nodiscard]] auto CreateBackup(std::string const& p, std::string const& pw) const -> bool {
    return sm_->account->CreateBackup(p, pw);
  }

  auto RestoreBackup(std::string const& p, std::string const& pw) -> bool {
    Terminate();
    bool const success = sm_->account->RestoreBackup(p, pw);
    if (success) {
      EventQueue::Instance().Push(EventReloadContactCache{});
      EventQueue::Instance().Push(EventDataReloaded{});
      Start();
      BroadcastProfileUpdate();
      return true;
    }
    Start();
    return false;
  }

  void UpdateSignalingUrl(std::string const& url) const {
    sm_->account->UpdateSignalingUrl(url);
  }

  void UpdateMyProfile(identity::ProfileUpdate const& profile) {
    sm_->account->UpdateMyProfile(profile);
    BroadcastProfileUpdate();
  }

  void UpdateIceConfig(IceConfigParams const& params) {
    sm_->account->GetSettings()->SetCustomIceConfig(params);
    ReevaluateIceConfiguration();
  }

  [[nodiscard]] auto GetSettingsManager() const -> SettingsManager* {
    return sm_->account->GetSettings();
  }

  [[nodiscard]] auto Sign(std::span<std::byte const> const message) const -> std::vector<std::byte> {
    return sm_->account->Sign(message);
  }

  [[nodiscard]] auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage> {
    return sm_->storage;
  }

  auto AddContact(ContactID const& id, std::optional<std::vector<std::byte>> const& t) -> bool {
    return sm_->contact->AddContact(id, t);
  }
  [[nodiscard]] auto GenerateContactToken() const -> std::vector<std::byte> {
    return sm_->contact->GenerateContactToken();
  }
  void ConfirmContact(ContactID const& id) const {
    sm_->contact->ConfirmContact(id);
  }
  void BlockContact(ContactID const& id, bool n) const {
    sm_->contact->BlockContact(id, n);
  }
  void DeleteContact(ContactID const& id) const {
    sm_->contact->DeleteContact(id);
  }
  void UnblockContact(ContactID const& id) const {
    sm_->contact->UnblockContact(id);
  }
  void SetContactDisplayNameOverride(ContactID const& id, std::string const& n) const {
    sm_->contact->SetContactDisplayNameOverride(id, n);
  }
  void ResetContactDisplayNameOverride(ContactID const& id) const {
    sm_->contact->ResetContactDisplayNameOverride(id);
  }
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact> {
    return sm_->contact->GetContacts();
  }

  auto SendChatMessage(
      ContactID const& id,
      std::string const& msg,
      std::vector<messaging::AttachmentRequest> const& attachments = {}
  ) const -> messaging::HistoryEntryRecord {
    return sm_->messaging->SendChatMessage(id, msg, attachments);
  }
  void MarkMessagesAsRead(ContactID const& id, std::vector<std::uint64_t> const& mids) const {
    sm_->messaging->MarkMessagesAsRead(id, mids);
  }
  [[nodiscard]] auto GetMessageHistoryPage(ContactID const& id, size_t const page_index, size_t const page_size) const
      -> messaging::HistoryPage {
    return sm_->messaging->GetHistoryPage(id, page_index, page_size);
  }
  [[nodiscard]] auto GetFullMessageContent(ContactID const& id, uint64_t const msg_id) const
      -> std::optional<std::string> {
    return sm_->messaging->GetFullMessageContent(id, msg_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& ids) const -> std::vector<messaging::ChatSummary> {
    return sm_->messaging->GetChatSummaries(ids);
  }
  [[nodiscard]] auto GetUnreadMessages(ContactID const& id) const -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->GetUnreadMessages(id);
  }
  [[nodiscard]] auto SearchMessages(ContactID const& peer_id, std::string const& query) const
      -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->SearchMessages(peer_id, query);
  }
  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->SearchAllMessages(query);
  }
  [[nodiscard]] auto GetHistoryEntry(ContactID const& peer_id, uint64_t message_id) const
      -> std::optional<messaging::HistoryEntryRecord> {
    return sm_->storage->GetHistoryEntry(peer_id, message_id);
  }

  void CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) const {
    sm_->messaging->CancelTransfer(peer_id, attachment_id, is_upload);
  }
  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) const {
    sm_->messaging->ResumeTransfer(peer_id, attachment_id, is_upload);
  }
  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) const {
    sm_->messaging->DiscardAttachment(peer_id, attachment_id);
  }
  [[nodiscard]] auto GetAttachment(std::string const& attachment_id) const -> std::optional<messaging::Attachment> {
    return sm_->storage->GetAttachment(attachment_id);
  }

  void Connect(ContactID const& peer_id) const {
    auto const peer = sm_->data_transport->GetPeer(peer_id);
    if (peer && peer->data_channel_is_open) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      return;
    }
    auto const state = sm_->data_transport->GetConnectionState(peer_id);
    if (state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection) {
      return;
    }
    if (!peer) {
      auto const new_peer = std::make_shared<transport::Peer>(peer_id);
      sm_->data_transport->AddPeer(peer_id, new_peer);
    }
    sm_->data_transport->Connect(peer_id);
    sm_->data_transport->CreateOffer(peer_id);
  }

  void Disconnect(ContactID const& peer_id) const {
    sm_->data_transport->Disconnect(peer_id);
    sm_->data_transport->RemovePeer(peer_id);
  }

  void StartVoiceCall(ContactID const& id) const {
    sm_->media_call->StartVoiceCall(id);
  }
  void StartVideoCall(ContactID const& id) const {
    sm_->media_call->StartVideoCall(id);
  }
  void AcceptCall(ContactID const& id) const {
    sm_->media_call->AcceptCall(id);
  }
  void RejectCall(ContactID const& id) const {
    sm_->media_call->RejectCall(id);
  }
  void Hangup(ContactID const& id, bool n) const {
    sm_->media_call->Hangup(id, n);
  }

  [[nodiscard]] auto GetCurrentCallState() const -> view_model::CallState {
    auto session = sm_->media_call->GetCurrentSession();
    if (!session) return {};
    return {
        .is_connecting = !session->is_active && !session->is_ringing,
        .is_in_call = session->is_active,
        .is_ringing = session->is_ringing,
        .call_direction =
            session->is_outgoing ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
        .peer_id = session->peer_id,
        .call_type = session->is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall
    };
  }

  [[nodiscard]] auto GetPeerConnectionState(ContactID const& id) const -> transport::PeerConnectionState {
    return sm_->data_transport->GetConnectionState(id);
  }

  void DebugEraseAllHistory() const {
    sm_->storage->DebugClearAllHistory();
  }
  void DebugEraseAllContacts() const {
    sm_->storage->DebugClearAllContacts();
    EventQueue::Instance().Push(EventDebugClearContactCache{});
    sm_->storage->DebugClearAllHistory();
  }
  void DebugRevokeAllContactApprovals() const {
    sm_->storage->DebugRevokeAllContactApprovals();
    EventQueue::Instance().Push(EventReloadContactCache{});
  }

  [[nodiscard]] auto GetAllPendingActions() const -> std::vector<view_model::DebugPendingAction> {
    std::vector<view_model::DebugPendingAction> result;
    auto contacts = sm_->contact->GetContacts();
    for (auto const& contact : contacts) {
      auto actions = sm_->storage->GetPendingActions(contact.peer_id);
      for (auto const& action : actions) {
        result.push_back({action.peer_id, action.id, action.payload});
      }
    }
    return result;
  }

  [[nodiscard]] auto GetConnectionDebugInfo() const -> std::vector<view_model::DebugConnectionInfo> {
    std::vector<view_model::DebugConnectionInfo> result;
    auto now = Time::Now();
    auto peers = sm_->data_transport->GetPeers();
    auto contacts = sm_->contact->GetContacts();
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
      result.push_back(
          {id,
           sm_->data_transport->GetConnectionState(id),
           ago,
           peer->data_channel_is_open.load(),
           peer->has_media_peer_connection.load()}
      );
    }
    return result;
  }

  [[nodiscard]] auto GetAllUnreadInfo() const -> std::vector<view_model::DebugUnreadInfo> {
    std::vector<view_model::DebugUnreadInfo> result;
    auto contacts = sm_->contact->GetContacts();
    std::vector<ContactID> ids;
    for (auto const& c : contacts) ids.push_back(c.peer_id);
    auto summaries = sm_->messaging->GetChatSummaries(ids);
    for (auto const& s : summaries) {
      if (s.unread_count > 0) result.push_back({s.peer_id, s.unread_count, s.last_message.timestamp});
    }
    return result;
  }

 private:
  void CheckForInactivePeers() {
    if (!is_running_) return;
    auto const now = Time::Now();
    auto const timeout =
        std::chrono::duration_cast<std::chrono::microseconds>(sm_->account->GetSettings()->GetPeerInactivityTimeout())
            .count();

    for (auto const& [id, peer] : sm_->data_transport->GetPeers()) {
      bool const is_data_connected =
          sm_->data_transport->GetConnectionState(id) == transport::PeerConnectionState::connected;
      if (peer->has_media_peer_connection) {
        peer->last_activity_time = now;
        if (!peer->data_channel_is_open) Connect(id);
      } else if (is_data_connected) {
        if ((now - peer->last_activity_time.load()) > static_cast<uint64_t>(timeout)) {
          LGI << "Peer " << id.ToString() << " inactive. Closing.";
          sm_->data_transport->Disconnect(id);
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
          EventQueue::Instance().Push(EventFatalError{});
          std::unique_lock<std::mutex> lock(watchdog_mutex_);
          if (watchdog_cv_.wait_for(lock, std::chrono::seconds(10), [this] { return !watchdog_running_; })) break;
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
    sm_->profile->MarkAllContactsProfileSyncNeeded();
    auto const contacts = sm_->contact->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed ||
          contact.status == identity::ContactStatus::pendingApproval) {
        Connect(contact.peer_id);
      }
    }
  }

  void ReevaluateIceConfiguration() {
    Json final_config = Json::array();
    auto* settings = sm_->account->GetSettings();
    if (auto stun = settings->GetCustomStun(); stun && !stun->empty()) {
      final_config.push_back({{"urls", std::vector<std::string>{*stun}}});
    }
    if (auto turn = settings->GetCustomTurn(); turn && !turn->empty()) {
      Json turn_server;
      turn_server["urls"] = std::vector<std::string>{*turn};
      if (auto user = settings->GetCustomTurnUser(); user && !user->empty()) turn_server["username"] = *user;
      if (auto pass = settings->GetCustomTurnPass(); pass && !pass->empty()) turn_server["credential"] = *pass;
      final_config.push_back(turn_server);
    }
    if (final_config.empty() && !server_ice_config_.empty()) final_config = server_ice_config_;
    if (!final_config.empty()) {
      auto json_str = final_config.dump();
      sm_->data_transport->SetIceServers(json_str);
      sm_->media_transport->SetIceServers(json_str);
    }
  }

  std::unique_ptr<services::ServiceManager> sm_;

  std::atomic<bool> is_running_{false};
  std::vector<std::function<void()>> event_unsubs_;

  std::thread watchdog_thread_;
  std::atomic<bool> watchdog_running_{false};
  std::condition_variable watchdog_cv_;
  std::mutex watchdog_mutex_;
  std::atomic<uint64_t> last_heartbeat_{0};

  Json server_ice_config_{Json::array()};

  static constexpr std::chrono::seconds peerInactivityCheckInterval{20};
  DispatchQueue background_queue_{"ClientBgQueue", 1};
};

}  // namespace eillance
