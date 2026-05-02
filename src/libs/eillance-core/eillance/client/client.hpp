#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/delivery_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/internal_messaging_service.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/libp2p_data_transport.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/watchdog.hpp"
#include "eillance/view_model/view_model_defines.hpp"

namespace eillance {

class Client final {
 public:
  using Json = nlohmann::json;

  Client(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::MediaTransport> media_transport,
      std::shared_ptr<transport::DataTransport> data_transport = nullptr,
      transport::NodeRole role = transport::NodeRole::desktopClient
  ) {
    sm_ = std::make_unique<services::ServiceManager>();
    sm_->storage = std::move(storage);

    if (data_transport) {
      sm_->data_transport = std::move(data_transport);
    } else {
      auto libp2p_dt = std::make_unique<transport::Libp2pDataTransport>(role);
      sm_->data_transport = std::make_shared<transport::DataTransport>(std::move(libp2p_dt));
    }
    sm_->media_transport = std::move(media_transport);
    sm_->contact_manager = std::make_shared<identity::ContactManager>(sm_->storage);

    sm_->account = std::make_shared<services::AccountService>(sm_.get());
    sm_->file_transfer = std::make_shared<services::FileTransferService>(sm_.get());
    sm_->messaging = std::make_shared<services::MessagingService>(sm_.get());
    sm_->media_call = std::make_shared<services::MediaCallService>(sm_.get());
    sm_->profile = std::make_shared<services::ProfileService>(sm_.get());
    sm_->contact = std::make_shared<services::ContactService>(sm_.get());
    sm_->internal_messaging = std::make_shared<services::InternalMessagingService>(sm_.get());
    sm_->sync_engine = std::make_shared<sync::SyncEngine>(sm_->storage, sm_->data_transport, sm_->account->GetId());
    sm_->projection_service = std::make_shared<services::LedgerProjectionService>(sm_.get());
    sm_->delivery_service = std::make_shared<messaging::DeliveryService>(sm_.get());

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventAppForegrounded>([this](auto const&) -> void {
      sm_->is_foreground = true;
    }));

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventAppBackgrounded>([this](auto const&) -> void {
      sm_->is_foreground = false;
    }));

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventDataPeerConnectionStateChanged>([this](auto const& event) -> void {
          auto const state = event.state;
          if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
              state == transport::PeerConnectionState::disconnected) {
            auto const current_state = sm_->data_transport->GetConnectionState(event.peer_id);
            if (current_state != transport::PeerConnectionState::failed &&
                current_state != transport::PeerConnectionState::closed &&
                current_state != transport::PeerConnectionState::disconnected) {
              LGI << "Ignoring stale data connection state event for " << event.peer_id.ToString();
              return;
            }

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
        })
    );

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventServiceConnectionStateChanged>([this](auto const& event) -> void {
          auto const state = event.state;
          if (state == ServiceConnectionState::failure || state == ServiceConnectionState::noNetwork ||
              state == ServiceConnectionState::disconnected) {
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
              sm_->sync_engine->Start();
              sm_->data_transport->DiscoverTurnRelays();
            }
          }
        })
    );

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventMediaPeerConnectionStateChanged>([this](auto const& event) -> void {
          auto const state = event.state;
          if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed) {
            auto const current_state = sm_->media_transport->GetConnectionState(event.peer_id);
            if (current_state != transport::PeerConnectionState::failed &&
                current_state != transport::PeerConnectionState::closed &&
                current_state != transport::PeerConnectionState::disconnected) {
              return;
            }
            sm_->media_transport->Disconnect(event.peer_id);
          }
        })
    );

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) -> void {
          background_queue_.DispatchAfter(std::chrono::seconds(2), [this, peer_id = e.peer_id]() {
            sm_->data_transport->AuthTurnRelay(peer_id);
          });
        })
    );

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventTurnRelaysDiscovered>([this](auto const& e) -> void {
          auto relays = e.peer_ids;
          std::mt19937 g(std::random_device{}());
          std::shuffle(relays.begin(), relays.end(), g);
          int count = 0;
          for (auto const& r : relays) {
            if (count++ >= 10) break;
            sm_->data_transport->AuthTurnRelay(r);
          }
        })
    );

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventTurnAuthResult>([this](auto const& e) -> void {
      if (e.success && e.port > 0 && !e.public_ip.empty()) {
        LGI << "[TURN] Dynamic TURN server discovered and authenticated: " << e.public_ip << ":" << e.port << " user: " << e.username;
        {
          std::lock_guard<std::mutex> const lock(dynamic_turn_mutex_);
          std::erase_if(dynamic_turn_servers_, [&](auto const& s) { return s.peer_id == e.peer_id; });
          dynamic_turn_servers_.push_back(
              {e.peer_id, e.public_ip, e.port, e.username, e.credential, std::chrono::steady_clock::now()}
          );
          if (dynamic_turn_servers_.size() > 5) dynamic_turn_servers_.erase(dynamic_turn_servers_.begin());
        }
        ReevaluateIceConfiguration();
      }
    }));

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventTurnEligibilityChanged>([this](auto const& e) -> void {
          sm_->data_transport->SetTurnEligibility(e.is_eligible);
        })
    );

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventConnectionRequested>(
        [this](auto const& e) -> void { this->Connect(e.peer_id); }
    ));
  }

  Client(Client const&) = delete;
  Client(Client&&) = delete;
  auto operator=(Client const&) -> Client& = delete;
  auto operator=(Client&&) -> Client& = delete;
  ~Client() {
    for (auto const& evnt : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(evnt);
    }
    Terminate();
  }

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

  void Start() {
    if (!sm_->account->IdentityExists()) {
      LGE << "Client started without an identity.";
      return;
    }
    sm_->sync_engine->SetMyId(sm_->account->GetId());
    if (is_running_.exchange(true)) {
      return;
    }
    ReevaluateIceConfiguration();
    if (auto const url = sm_->account->GetSettings()->GetBootstrapNodeUrl()) {
      sm_->data_transport->SetBootstrapNodeUrl(*url);
    }

    sm_->data_transport->SetId(sm_->account->GetId());
    sm_->data_transport->SetSecretKey(sm_->account->GetPrivateKey());
    sm_->data_transport->Start();
    sm_->sync_engine->Start();
    sm_->delivery_service->Start();
    StartWatchdog();
    background_queue_.Dispatch([this]() -> void { CheckForInactivePeers(); });
  }

  void Terminate() {
    is_running_ = false;
    StopWatchdog();
    background_queue_.RemovePending();

    sm_->delivery_service->Stop();
    sm_->sync_engine->Stop();

    auto peers = sm_->data_transport->GetPeers();
    for (auto const& id : peers | std::views::keys) {
      sm_->media_transport->Disconnect(id);
      sm_->data_transport->Disconnect(id);
      sm_->data_transport->RemovePeer(id);
    }
    sm_->data_transport->Terminate();
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
    Start();
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

  void UpdateBootstrapNodeUrl(std::string const& url) const {
    sm_->account->UpdateBootstrapNodeUrl(url);
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

  void UpdatePlatformPermissions(bool notif, bool batt, bool calls) const {
    sm_->account->UpdatePlatformPermissions(notif, batt, calls);
  }

  void IgnoreBatteryOptimization() const {
    sm_->account->IgnoreBatteryOptimization();
  }

  void IgnoreCallPermissions() const {
    sm_->account->IgnoreCallPermissions();
  }

  void ResetIgnoredPermissions() const {
    sm_->account->ResetIgnoredPermissions();
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
      std::vector<messaging::AttachmentRequest> const& attachments = {},
      std::uint64_t reply_to_id = 0
  ) -> messaging::HistoryEntryRecord {
    return sm_->messaging->SendChatMessage(id, msg, attachments, reply_to_id);
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
  [[nodiscard]] auto GetDownloadedAttachments(ContactID const& peer_id
  ) const -> std::vector<messaging::AttachmentRecord> {
    return sm_->messaging->GetDownloadedAttachments(peer_id);
  }

  void DeleteAttachmentLocal(ContactID const& peer_id, std::string const& attachment_id) const {
    sm_->messaging->DeleteAttachmentLocal(peer_id, attachment_id);
  }

  void Connect(ContactID const& peer_id) const {
    auto const contact = sm_->contact->GetContact(peer_id);
    if (contact && contact->status == identity::ContactStatus::rejected) {
      return;
    }

    auto const peer = sm_->data_transport->GetPeer(peer_id);
    if (peer && peer->data_channel_is_open) {
      return;
    }
    auto const state = sm_->data_transport->GetConnectionState(peer_id);
    if (state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection ||
        state == transport::PeerConnectionState::connected) {
      return;
    }
    if (!peer) {
      auto const new_peer = std::make_shared<transport::Peer>(peer_id);
      sm_->data_transport->AddPeer(peer_id, new_peer);
    } else {
      peer->last_activity_time = Time::Now();
    }
    sm_->data_transport->Connect(peer_id);
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
    return {};
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
      std::erase_if(result, [&](auto const& item) -> bool { return item.peer_id == id; });
      auto const lat = peer->last_activity_time.load();
      int64_t ago = 0;
      if (now > lat) {
        ago = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds(now - lat)).count();
      }
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
      auto const state = sm_->data_transport->GetConnectionState(id);
      bool const is_data_connected = state == transport::PeerConnectionState::connected;
      bool const is_connecting =
          state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection;

      if (peer->has_media_peer_connection) {
        peer->last_activity_time = now;
        if (!peer->data_channel_is_open) Connect(id);
      } else if (is_data_connected) {
        auto const lat = peer->last_activity_time.load();
        if (now > lat && (now - lat) > static_cast<uint64_t>(timeout)) {
          LGI << "Peer " << id.ToString() << " inactive. Closing.";
          sm_->data_transport->Disconnect(id);
        }
      } else if (is_connecting) {
        auto const lat = peer->last_activity_time.load();
        if (now > lat && (now - lat) > 45ULL * 1000000ULL) {
          LGI << "Peer " << id.ToString() << " stuck in connecting. Closing.";
          EventQueue::Instance().Push(EventConnectionAttemptFinished{id});
          sm_->data_transport->Disconnect(id);
        }
      }
    }

    static int turn_discovery_counter = 0;
    if (++turn_discovery_counter >= 15) {
      turn_discovery_counter = 0;
      sm_->data_transport->DiscoverTurnRelays();
    }

    background_queue_.DispatchAfter(peerInactivityCheckInterval, [this] { CheckForInactivePeers(); });
  }

  void StartWatchdog() {
    watchdog_ = std::make_unique<utils::Watchdog>(background_queue_, std::chrono::seconds(15), []() {
      LGE << "Core Watchdog: Main queue stalled. Triggering fatal error.";
      EventQueue::Instance().Push(EventFatalError{});
    });
    watchdog_->Start();
  }

  void StopWatchdog() {
    if (watchdog_) {
      watchdog_->Stop();
      watchdog_.reset();
    }
  }

  void BroadcastProfileUpdate() {
    sm_->profile->MarkAllContactsProfileSyncNeeded();
    auto const contacts = sm_->contact->GetContacts();
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
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

    {
      std::lock_guard<std::mutex> const lock(dynamic_turn_mutex_);
      auto const now = std::chrono::steady_clock::now();
      std::erase_if(dynamic_turn_servers_, [&](auto const& s) {
        return (now - s.discovered_at) > std::chrono::minutes(10);
      });

      for (auto const& ts : dynamic_turn_servers_) {
        std::string ip_str = ts.ip;
        if (ip_str.find(':') != std::string::npos) {
          ip_str = "[" + ip_str + "]";
        }

        Json turn_server;
        turn_server["urls"] = std::vector<std::string>{
            "turn:" + ip_str + ":" + std::to_string(ts.port) + "?transport=udp"
        };
        turn_server["username"] = ts.username;
        turn_server["credential"] = ts.credential;
        final_config.push_back(turn_server);

        Json stun_server;
        stun_server["urls"] = std::vector<std::string>{
            "stun:" + ip_str + ":" + std::to_string(ts.port)
        };
        final_config.push_back(stun_server);
      }
    }

    auto json_str = final_config.dump();
    LGI << "[ICE] Final WebRTC Configuration: " << json_str;
    sm_->media_transport->SetIceServers(json_str);
  }

  std::unique_ptr<services::ServiceManager> sm_;

  std::atomic<bool> is_running_{false};
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unique_ptr<utils::Watchdog> watchdog_;

  static constexpr std::chrono::seconds peerInactivityCheckInterval{20};
  DispatchQueue background_queue_{"ClientBgQueue", 1};

  struct DynamicTurnServer {
    ContactID peer_id;
    std::string ip;
    uint16_t port;
    std::string username;
    std::string credential;
    std::chrono::steady_clock::time_point discovered_at;
  };
  std::vector<DynamicTurnServer> dynamic_turn_servers_;
  std::mutex dynamic_turn_mutex_;
};

}  // namespace eillance
