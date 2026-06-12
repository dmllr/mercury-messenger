#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/channel_manager.hpp"
#include "eillance/identity/group_manager.hpp"
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
    sm_->group_manager = std::make_shared<identity::GroupManager>(sm_->storage);
    sm_->channel_manager = std::make_shared<identity::ChannelManager>(sm_->storage);

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
      Connect(sm_->account->GetId());
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
              LGD << "Ignoring stale data connection state event for " << event.peer_id.ToString();
              return;
            }

            sm_->data_transport->Disconnect(event.peer_id);

            bool media_active = false;
            if (auto peer = sm_->data_transport->GetPeer(event.peer_id)) {
              media_active = peer->has_media_peer_connection;
            }

            if (media_active && last_network_state_ != ServiceConnectionState::noNetwork &&
                last_network_state_ != ServiceConnectionState::failure &&
                last_network_state_ != ServiceConnectionState::disconnected) {
              LGI << "Data connection lost for " << event.peer_id.ToString()
                  << " but media is active. Reconnecting data.";
              EventQueue::Instance().Push(EventConnectionAttemptFinished{event.peer_id});
              Connect(event.peer_id);
            } else {
              auto const media_state = sm_->media_transport->GetConnectionState(event.peer_id);
              if (media_state != transport::PeerConnectionState::disconnected &&
                  media_state != transport::PeerConnectionState::closed &&
                  media_state != transport::PeerConnectionState::failed) {
                EventQueue::Instance().Push(EventMediaPeerConnectionStateChanged{event.peer_id, state});
                sm_->media_transport->Disconnect(event.peer_id);
              }
              sm_->data_transport->RemovePeer(event.peer_id);
              EventQueue::Instance().Push(EventConnectionAttemptFinished{event.peer_id});
            }
          }
        })
    );

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventServiceConnectionStateChanged>([this](auto const& event) -> void {
          auto const state = event.state;

          if (state == ServiceConnectionState::online && last_network_state_ == ServiceConnectionState::online) {
            LGI << "Network interface change detected (online -> online). Reconnecting transport.";
            if (this->is_running_.load()) {
              for (auto const& [id, peer] : sm_->data_transport->GetPeers()) {
                EventQueue::Instance().Push(EventConnectionAttemptFinished{sm_->data_transport->GetMasterId(id)});
              }
              sm_->data_transport->Reconnect();
              sm_->data_transport->DiscoverTurnRelays();
            }
            return;
          }

          if (state == last_network_state_) {
            return;
          }
          last_network_state_ = state;

          if (state == ServiceConnectionState::failure || state == ServiceConnectionState::noNetwork ||
              state == ServiceConnectionState::disconnected) {
            std::unordered_set<ContactID> processed_masters;
            for (auto const& [id, peer] : sm_->data_transport->GetPeers()) {
              auto const master_id = sm_->data_transport->GetMasterId(id);
              if (processed_masters.contains(master_id)) {
                continue;
              }
              processed_masters.insert(master_id);

              sm_->data_transport->Disconnect(master_id);
              sm_->media_transport->Disconnect(master_id);
              EventQueue::Instance().Push(EventConnectionAttemptFinished{master_id});
            }
          } else if (state == ServiceConnectionState::online) {
            if (this->is_running_.load()) {
              sm_->data_transport->Reconnect();
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

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) -> void {
      background_queue_.DispatchAfter(std::chrono::seconds(2), [this, peer_id = e.peer_id]() -> void {
        sm_->data_transport->AuthTurnRelay(peer_id);
      });
    }));

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventTurnRelaysDiscovered>([this](auto const& e) -> void {
          auto relays = e.peer_ids;
          std::mt19937 g(std::random_device{}());
          std::shuffle(relays.begin(), relays.end(), g);
          int count = 0;
          for (auto const& r : relays) {
            if (count++ >= 10) {
              break;
            }
            sm_->data_transport->AuthTurnRelay(r);
          }
        })
    );

    event_unsubs_.emplace_back(EventQueue::Instance().Subscribe<EventTurnAuthResult>([this](auto const& e) -> void {
      if (e.success && e.port > 0 && !e.public_ip.empty()) {
        LGI << "[TURN] Dynamic TURN server discovered and authenticated: " << e.public_ip << ":" << e.port
            << " user: " << e.username;
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

    event_unsubs_.emplace_back(
        EventQueue::Instance().Subscribe<EventConnectionRequested>([this](auto const& e) -> void {
          this->Connect(e.peer_id);
        })
    );
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
    if (auto const urls = sm_->account->GetSettings()->GetBootstrapNodeUrls()) {
      sm_->data_transport->SetBootstrapNodeUrls(SettingsManager::ParseUrls(*urls));
    }

    sm_->data_transport->SetId(sm_->account->GetId());
    sm_->data_transport->SetDeviceId(ContactID(sm_->account->GetDevicePublicKey()));
    sm_->data_transport->SetSecretKey(sm_->account->GetDevicePrivateKey());
    sm_->data_transport->Start();
    sm_->sync_engine->Start();
    sm_->delivery_service->Start();
    StartWatchdog();

    // Immediately initiate relay discovery to prevent ICE config race conditions prior to the first call
    sm_->data_transport->DiscoverTurnRelays();

    background_queue_.Dispatch([this]() -> void { CheckForInactivePeers(); });
  }

  void Terminate() {
    is_running_ = false;
    StopWatchdog();
    background_queue_.RemovePending();

    sm_->delivery_service->Stop();
    sm_->sync_engine->Stop();

    std::unordered_set<ContactID> processed_masters;
    auto peers = sm_->data_transport->GetPeers();
    for (auto const& id : peers | std::views::keys) {
      auto const master_id = sm_->data_transport->GetMasterId(id);
      if (processed_masters.contains(master_id)) continue;
      processed_masters.insert(master_id);

      sm_->media_transport->Disconnect(master_id);
      sm_->data_transport->Disconnect(master_id);
      sm_->data_transport->RemovePeer(master_id);
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

  void UpdateBootstrapNodeUrls(std::string const& urls) const {
    sm_->account->UpdateBootstrapNodeUrls(urls);
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

  auto CreateGroup(
      std::string const& name,
      std::vector<ContactID> const& member_ids,
      std::optional<std::string> const& photo_base64 = std::nullopt
  ) -> GroupID {
    auto gid = identity::GroupManager::GenerateGroupID();
    identity::Group g;
    g.id = gid;
    g.name = name;
    g.owner_id = sm_->account->GetId();
    g.members.push_back({g.owner_id, 1});
    for (auto const& m : member_ids) {
      g.members.push_back({m, 0});
    }

    Json payload = {{"type", "group_create"}, {"name", name}, {"owner_id", g.owner_id.ToString()}};
    ApplyPhotoUpdate(g.photo_base64, payload, photo_base64);

    sm_->group_manager->CreateGroupLocal(g);

    Json members_array = Json::array();
    for (auto const& m : g.members) {
      members_array.push_back({{"contact_id", m.contact_id.ToString()}, {"role", m.role}});
    }
    payload["members"] = members_array;

    sm_->sync_engine->AppendAction(TopicID(gid), messaging::LedgerActionType::GroupState, payload);

    Json sys_msg = {{"action", "group_created"}, {"name", name}};
    sm_->messaging->SendSystemMessage(TopicID(gid), sys_msg.dump(), true);

    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return gid;
  }

  void UpdateGroup(
      GroupID const& gid,
      std::optional<std::string> const& name,
      std::optional<std::string> const& photo_base64
  ) {
    auto g = sm_->group_manager->GetGroup(gid);
    if (!g || g->owner_id != sm_->account->GetId()) return;

    if (name) g->name = *name;

    Json payload = {{"type", "group_update"}};
    if (name) payload["name"] = *name;

    ApplyPhotoUpdate(g->photo_base64, payload, photo_base64);

    sm_->group_manager->CreateGroupLocal(*g);

    sm_->sync_engine->AppendAction(TopicID(gid), messaging::LedgerActionType::GroupState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
  }

  auto AddGroupMember(GroupID const& gid, ContactID const& member_id) -> bool {
    auto g = sm_->group_manager->GetGroup(gid);
    if (!g || g->owner_id != sm_->account->GetId()) return false;
    sm_->group_manager->AddMemberLocal(gid, member_id, 0);
    Json payload = {{"type", "group_member_add"}, {"contact_id", member_id.ToString()}, {"role", 0}};
    sm_->sync_engine->AppendAction(TopicID(gid), messaging::LedgerActionType::GroupState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return true;
  }

  auto RemoveGroupMember(GroupID const& gid, ContactID const& member_id) -> bool {
    auto g = sm_->group_manager->GetGroup(gid);
    if (!g || g->owner_id != sm_->account->GetId()) return false;
    sm_->group_manager->RemoveMemberLocal(gid, member_id);
    Json payload = {{"type", "group_member_remove"}, {"contact_id", member_id.ToString()}};
    sm_->sync_engine->AppendAction(TopicID(gid), messaging::LedgerActionType::GroupState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return true;
  }

  void LeaveGroup(GroupID const& gid) {
    auto g = sm_->group_manager->GetGroup(gid);
    if (!g) return;
    sm_->group_manager->RemoveMemberLocal(gid, sm_->account->GetId());
    Json payload = {{"type", "group_member_remove"}, {"contact_id", sm_->account->GetId().ToString()}};
    sm_->sync_engine->AppendAction(TopicID(gid), messaging::LedgerActionType::GroupState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
  }

  [[nodiscard]] auto GetGroupMembers(GroupID const& gid) const -> std::vector<identity::GroupMember> {
    if (auto g = sm_->group_manager->GetGroup(gid)) {
      return g->members;
    }
    return {};
  }

  [[nodiscard]] auto GetGroups() const -> std::vector<identity::Group> {
    return sm_->group_manager->GetGroups();
  }

  auto CreateChannel(std::string const& name, std::string const& description) -> ChannelID {
    auto cid = identity::ChannelManager::GenerateChannelID();
    identity::Channel c;
    c.id = cid;
    c.name = name;
    c.description = description;
    c.owner_id = sm_->account->GetId();
    c.members.push_back({c.owner_id, 1});
    sm_->channel_manager->CreateChannelLocal(c);
    Json payload =
        {{"type", "channel_create"}, {"name", name}, {"description", description}, {"owner_id", c.owner_id.ToString()}};
    sm_->sync_engine->AppendAction(TopicID(cid), messaging::LedgerActionType::ChannelState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return cid;
  }
  void SubscribeToChannel(ChannelID const& cid, ContactID const& owner_id) {
    auto c = sm_->channel_manager->GetChannel(cid);
    if (!c) {
      identity::Channel stub;
      stub.id = cid;
      stub.name = "Syncing Channel...";
      stub.owner_id = owner_id;
      sm_->channel_manager->CreateChannelLocal(stub);
    }
    sm_->channel_manager->AddMemberLocal(cid, sm_->account->GetId(), 0);
    Json payload = {{"type", "channel_subscribe"}, {"contact_id", sm_->account->GetId().ToString()}};
    sm_->sync_engine->AppendAction(TopicID(cid), messaging::LedgerActionType::ChannelState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
  }
  void UnsubscribeChannel(ChannelID const& cid) {
    sm_->channel_manager->RemoveMemberLocal(cid, sm_->account->GetId());
    Json payload = {{"type", "channel_unsubscribe"}, {"contact_id", sm_->account->GetId().ToString()}};
    sm_->sync_engine->AppendAction(TopicID(cid), messaging::LedgerActionType::ChannelState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
  }
  auto GetChannels() const -> std::vector<identity::Channel> {
    return sm_->channel_manager->GetChannels();
  }
  auto GetChannelMembers(ChannelID const& cid) const -> std::vector<identity::ChannelMember> {
    if (auto c = sm_->channel_manager->GetChannel(cid)) return c->members;
    return {};
  }
  auto AddChannelMember(ChannelID const& cid, ContactID const& member_id) -> bool {
    auto c = sm_->channel_manager->GetChannel(cid);
    if (!c || c->owner_id != sm_->account->GetId()) return false;
    sm_->channel_manager->AddMemberLocal(cid, member_id, 0);
    Json payload = {{"type", "channel_member_add"}, {"contact_id", member_id.ToString()}, {"role", 0}};
    sm_->sync_engine->AppendAction(TopicID(cid), messaging::LedgerActionType::ChannelState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return true;
  }
  auto RemoveChannelMember(ChannelID const& cid, ContactID const& member_id) -> bool {
    auto c = sm_->channel_manager->GetChannel(cid);
    if (!c || c->owner_id != sm_->account->GetId()) return false;
    sm_->channel_manager->RemoveMemberLocal(cid, member_id);
    Json payload = {{"type", "channel_member_remove"}, {"contact_id", member_id.ToString()}};
    sm_->sync_engine->AppendAction(TopicID(cid), messaging::LedgerActionType::ChannelState, payload);
    EventQueue::Instance().Push(EventGroupsStateUpdated{});
    return true;
  }

  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact> {
    return sm_->contact->GetContacts();
  }

  auto SendChatMessage(
      TopicID const& topic_id,
      std::string const& msg,
      std::vector<messaging::AttachmentRequest> const& attachments = {},
      std::uint64_t reply_to_id = 0
  ) -> messaging::HistoryEntryRecord {
    return sm_->messaging->SendChatMessage(topic_id, msg, attachments, reply_to_id);
  }
  void MarkMessagesAsRead(TopicID const& id, std::vector<std::uint64_t> const& mids) const {
    sm_->messaging->MarkMessagesAsRead(id, mids);
  }
  [[nodiscard]] auto GetMessageHistoryPage(TopicID const& id, size_t const page_index, size_t const page_size) const
      -> messaging::HistoryPage {
    return sm_->messaging->GetHistoryPage(id, page_index, page_size);
  }
  [[nodiscard]] auto GetFullMessageContent(TopicID const& id, uint64_t const msg_id) const
      -> std::optional<std::string> {
    return sm_->messaging->GetFullMessageContent(id, msg_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<TopicID> const& ids) const -> std::vector<messaging::ChatSummary> {
    return sm_->messaging->GetChatSummaries(ids);
  }
  [[nodiscard]] auto GetUnreadMessages(TopicID const& id) const -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->GetUnreadMessages(id);
  }
  [[nodiscard]] auto SearchMessages(TopicID const& peer_id, std::string const& query) const
      -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->SearchMessages(peer_id, query);
  }
  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<messaging::HistoryEntryRecord> {
    return sm_->messaging->SearchAllMessages(query);
  }
  [[nodiscard]] auto GetHistoryEntry(TopicID const& peer_id, uint64_t message_id) const
      -> std::optional<messaging::HistoryEntryRecord> {
    return sm_->storage->GetHistoryEntry(peer_id, message_id);
  }

  void CancelTransfer(TopicID const& topic_id, std::string const& attachment_id, bool is_upload) const {
    sm_->messaging->CancelTransfer(topic_id, attachment_id, is_upload);
  }
  void ResumeTransfer(TopicID const& topic_id, std::string const& attachment_id, bool is_upload) const {
    sm_->messaging->ResumeTransfer(topic_id, attachment_id, is_upload);
  }
  void DiscardAttachment(TopicID const& topic_id, std::string const& attachment_id) const {
    sm_->messaging->DiscardAttachment(topic_id, attachment_id);
  }
  [[nodiscard]] auto GetAttachment(std::string const& attachment_id) const -> std::optional<messaging::Attachment> {
    return sm_->storage->GetAttachment(attachment_id);
  }
  [[nodiscard]] auto GetDownloadedAttachments(TopicID const& topic_id) const
      -> std::vector<messaging::AttachmentRecord> {
    return sm_->messaging->GetDownloadedAttachments(topic_id);
  }

  void DeleteAttachmentLocal(TopicID const& topic_id, std::string const& attachment_id) const {
    sm_->messaging->DeleteAttachmentLocal(topic_id, attachment_id);
  }

  void RetrySync(TopicID const& topic_id) const {
    auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
    if (group) {
      for (auto const& m : group->members) {
        sm_->delivery_service->ManualRetry(m.contact_id);
      }
    } else if (auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()))) {
      for (auto const& m : channel->members) {
        sm_->delivery_service->ManualRetry(m.contact_id);
      }
    } else {
      sm_->delivery_service->ManualRetry(ContactID(topic_id.Span()));
    }
  }

  [[nodiscard]] auto IsUnreachable(TopicID const& topic_id) const -> bool {
    auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
    if (group) {
      return false;  // Not supported directly, wait for retry
    }
    return sm_->delivery_service->IsUnreachable(ContactID(topic_id.Span()));
  }

  void Connect(ContactID const& peer_id) const {
    if (!is_running_.load()) {
      EventQueue::Instance().Push(EventConnectionAttemptFinished{peer_id});
      return;
    }

    if (peer_id != sm_->account->GetId()) {
      auto const contact = sm_->contact->GetContact(peer_id);
      if (contact && contact->status == identity::ContactStatus::rejected) {
        EventQueue::Instance().Push(EventConnectionAttemptFinished{peer_id});
        return;
      }
    }

    auto peer = sm_->data_transport->GetPeer(peer_id);
    if (peer && peer->data_channel_is_open) {
      EventQueue::Instance().Push(EventConnectionAttemptFinished{peer_id});
      return;
    }
    if (auto const state = sm_->data_transport->GetConnectionState(peer_id);
        state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection) {
      return;
    }

    auto const now = Time::SteadyNow();
    if (peer && (now - peer->last_connect_attempt.load() < 5000000ULL)) {
      EventQueue::Instance().Push(EventConnectionAttemptFinished{peer_id});
      return;
    }

    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      peer->last_connect_attempt = now;
      sm_->data_transport->AddPeer(peer_id, peer);
    } else {
      peer->last_connect_attempt = now;
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
    auto const session = sm_->media_call->GetCurrentSession();
    if (!session) {
      return {};
    }
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

 private:
  static void ApplyPhotoUpdate(
      std::optional<std::string>& target_field,
      Json& payload,
      std::optional<std::string> const& new_photo
  ) {
    if (new_photo) {
      if (new_photo->empty()) {
        target_field = std::nullopt;
        payload["photo"] = nullptr;
      } else {
        target_field = *new_photo;
        payload["photo"] = *new_photo;
      }
    }
  }

  void CheckForInactivePeers() {
    if (!is_running_) {
      return;
    }
    auto const now = Time::SteadyNow();
    auto const timeout =
        std::chrono::duration_cast<std::chrono::microseconds>(sm_->account->GetSettings()->GetPeerInactivityTimeout())
            .count();

    std::unordered_set<ContactID> disconnected_masters;

    for (auto const& [id, peer] : sm_->data_transport->GetPeers()) {
      auto const master_id = sm_->data_transport->GetMasterId(id);
      if (disconnected_masters.contains(master_id)) {
        continue;
      }

      auto const state = sm_->data_transport->GetConnectionState(id);
      bool const is_data_connected = state == transport::PeerConnectionState::connected;
      bool const is_connecting =
          state == transport::PeerConnectionState::connecting || state == transport::PeerConnectionState::newConnection;

      if (peer->has_media_peer_connection) {
        peer->last_activity_time = now;
        if (!peer->data_channel_is_open) {
          Connect(master_id);
        }
      } else if (is_data_connected) {
        if (master_id == sm_->account->GetId()) {
          // Keep connections to our own devices alive indefinitely to ensure seamless sync
          peer->last_activity_time = now;
        } else {
          auto const lat = peer->last_activity_time.load();
          if (now > lat && (now - lat) > static_cast<uint64_t>(timeout)) {
            LGI << "Peer " << master_id.ToString() << " inactive. Closing.";
            sm_->sync_engine->AbortSync(master_id);
            sm_->data_transport->Disconnect(master_id);
            sm_->data_transport->RemovePeer(master_id);
            EventQueue::Instance().Push(EventConnectionAttemptFinished{master_id});
            disconnected_masters.insert(master_id);
          }
        }
      } else if (is_connecting) {
        if (auto const lca = peer->last_connect_attempt.load(); now > lca && (now - lca) > 120ULL * Time::uS) {
          LGI << "Peer " << master_id.ToString() << " stuck in connecting. Closing.";
          sm_->sync_engine->AbortSync(master_id);
          sm_->data_transport->Disconnect(master_id);
          sm_->data_transport->RemovePeer(master_id);
          EventQueue::Instance().Push(EventConnectionAttemptFinished{master_id});
          disconnected_masters.insert(master_id);
        }
      }
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
    int delay_ms = 0;
    for (auto const& contact : contacts) {
      if (contact.status == identity::ContactStatus::confirmed) {
        background_queue_.DispatchAfter(std::chrono::milliseconds(delay_ms), [this, peer_id = contact.peer_id]() {
          Connect(peer_id);
        });
        delay_ms += 250;  // NOLINT(*-magic-numbers)
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
      if (auto user = settings->GetCustomTurnUser(); user && !user->empty()) {
        turn_server["username"] = *user;
      }
      if (auto pass = settings->GetCustomTurnPass(); pass && !pass->empty()) {
        turn_server["credential"] = *pass;
      }
      final_config.push_back(turn_server);
    }

    {
      std::lock_guard<std::mutex> const lock(dynamic_turn_mutex_);
      auto const now = std::chrono::steady_clock::now();
      std::erase_if(dynamic_turn_servers_, [&](auto const& s) -> bool {
        return (now - s.discovered_at) > std::chrono::minutes(10);
      });

      for (auto const& ts : dynamic_turn_servers_) {
        std::string ip_str = ts.ip;
        if (ip_str.find(':') != std::string::npos) {
          ip_str = "[" + ip_str + "]";
        }

        Json turn_server;
        turn_server["urls"] =
            std::vector<std::string>{"turn:" + ip_str + ":" + std::to_string(ts.port) + "?transport=udp"};
        turn_server["username"] = ts.username;
        turn_server["credential"] = ts.credential;
        final_config.push_back(turn_server);

        Json stun_server;
        stun_server["urls"] = std::vector<std::string>{"stun:" + ip_str + ":" + std::to_string(ts.port)};
        final_config.push_back(stun_server);
      }
    }

    auto json_str = final_config.dump();
    LGD << "[ICE] Final WebRTC Configuration: " << json_str;
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

  ServiceConnectionState last_network_state_{ServiceConnectionState::unknown};
};

}  // namespace eillance
