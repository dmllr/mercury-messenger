#pragma once

#include <memory>
#include <utility>

#include <nlohmann/json.hpp>

#include "eillance/capture/audio_capture.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/messaging/message_queue.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/candidate.hpp"
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
  using OnIncomingCallCallback = std::function<void(const std::string& peer_id, messaging::HistoryEntryKind call_type)>;

  explicit Client(std::shared_ptr<storage::PersistentStorage> storage, Capture video_capture, std::shared_ptr<transport::Transport> transport);

  Client(Client&&) = delete;
  Client(const Client&) = delete;
  Client& operator=(Client&&) = delete;
  Client& operator=(const Client&) = delete;
  ~Client() override = default;

  [[nodiscard]] auto getId() const -> std::string;
  auto getStorage() const -> std::shared_ptr<storage::PersistentStorage>;
  void setName(const std::string& name) const;
  void start() const;
  void terminate() const;
  void setStatusCallback(const StatusCallbackFunction& callback);
  void setFrameReceivedCallback(const OnFrameReceivedCallback& callback);
  void setProfileReceivedCallback(const OnProfileReceivedCallback& callback);
  void setChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback);
  void setContactRequestCallback(const OnContactRequestCallback& callback);
  void setContactStatusChangedCallback(const OnContactStatusChangedCallback& callback);
  void setMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback);
  void setPeerListUpdateCallback(const OnPeerListUpdateCallback& callback);
  void setIncomingCallCallback(const OnIncomingCallCallback& callback);
  void requestPeerListUpdate() const;
  void call(const std::string& peer_id);
  void connect(const std::string& peer_id);
  void startVoiceCall(const std::string& peer_id);
  void startVideoCall(const std::string& peer_id);
  void acceptCall(const std::string& peer_id);
  void rejectCall(const std::string& peer_id) const;
  void addContact(const std::string& peer_id) const;
  void confirmContact(const std::string& peer_id) const;
  auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState;
  auto getContacts() const -> std::vector<identity::Contact>;
  auto getChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary>;
  void hangup(const std::string& peer_id) const;
  void configure(const Config& config) const;
  auto sendChatMessage(const std::string& peer_id, const std::string& message) const -> messaging::HistoryEntryRecord;
  void markMessagesAsRead(const std::string& peer_id, const std::vector<std::uint64_t>& message_ids) const;
  auto getMessageHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage;

  // TransportDelegate implementation
  void onPeerReady(const std::shared_ptr<Peer>& peer) override;
  void onPeerOnline(const std::string& peer_id) override;
  void onPeerOffline(const std::string& peer_id) override;
  void onSignalingErrorForPeer(const std::string& peer_id, const std::string& error) override;
  void processPeerMessage(Json message) override;
  void processSystemMessage(const std::string& type, Json message) override;
  void onFrameReceived(const std::string& peer_id, const std::vector<std::byte>& frame) override;

private:
  void startMediaCall(const std::string& peer_id, bool is_video);
  void updateAndNotifyPeerList() const;
  auto directSend(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::string& message, std::uint64_t message_id) const -> bool;
  void sendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const;
  void processDataChannelMessage(const std::string& peer_id, const Json& message);
  void sendQueuedMessages(const std::string& peer_id) const;
  void setupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id);
  static auto parseDeviceProfile(const Json& jprofile) -> Device::Profile;

  std::shared_ptr<Stream> av_stream_;
  std::shared_ptr<transport::Transport> transport_;

  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  std::shared_ptr<OnChatMessageReceivedCallback> on_chat_message_received_{};
  std::shared_ptr<OnContactRequestCallback> on_contact_request_{};
  std::shared_ptr<OnContactStatusChangedCallback> on_contact_status_changed_{};
  std::shared_ptr<OnMessageStatusChangedCallback> on_message_status_changed_{};
  std::shared_ptr<OnPeerListUpdateCallback> on_peer_list_update_{};
  std::shared_ptr<StatusCallbackFunction> status_callback_{};
  std::shared_ptr<OnIncomingCallCallback> on_incoming_call_{};

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::IdManager> id_manager_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<messaging::MessageQueue> message_queue_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;
  std::unique_ptr<messaging::MessageHistory> message_history_;

  std::unordered_map<std::string, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;
};


// --- Implementation ---

inline Client::Client(std::shared_ptr<storage::PersistentStorage> storage, Capture video_capture, std::shared_ptr<transport::Transport> transport)
    : storage_(std::move(storage)),
      transport_(std::move(transport)) {

  transport_->setDelegate(this);

  id_manager_ = std::make_unique<identity::IdManager>(storage_);
  contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
  message_queue_ = std::make_shared<messaging::MessageQueue>(storage_);
  message_history_ = std::make_unique<messaging::MessageHistory>(storage_);

  av_stream_ = std::make_shared<Stream>(
      std::make_shared<StreamSource>(std::move(video_capture)),
      std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

  delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(
      message_queue_,
      contact_manager_,
      [this](const std::string& peer_id) {
        if (!transport_->getPeer(peer_id)) {
          connect(peer_id);
        }
      });

  transport_->setStatusCallback([this](const std::string& peer_id, const EventSource source, int state_val) {
    if (source == EventSource::peerConnectionState) {
      const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      if (state == eillance::transport::PeerConnectionState::Failed || state == eillance::transport::PeerConnectionState::Closed ||
          state == eillance::transport::PeerConnectionState::Disconnected) {
        delivery_service_->connectionAttemptFinished(peer_id);
      }
    }
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state_val);
    }
  });

  av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](const Stream::Source::Type type, std::uint64_t sample_time, const std::vector<std::byte>& sample) {
    sendSampleToPeers(sample, type, sample_time);
  });
}

inline auto Client::getId() const -> std::string {
  return id_manager_->GetId();
}
inline auto Client::getStorage() const -> std::shared_ptr<storage::PersistentStorage> {
  return storage_;
}
inline void Client::setName(const std::string& name) const {
  transport_->setName(name);
}
inline void Client::start() const {
  transport_->setId(id_manager_->GetId());
  delivery_service_->start();
  transport_->start();
}
inline void Client::terminate() const {
  delivery_service_->stop();
  transport_->terminate();
}
inline void Client::setStatusCallback(const StatusCallbackFunction& callback) {
  status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
}
inline void Client::setFrameReceivedCallback(const OnFrameReceivedCallback& callback) {
  on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback);
}
inline void Client::setProfileReceivedCallback(const OnProfileReceivedCallback& callback) {
  on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback);
}
inline void Client::setChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback) {
  on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback);
}
inline void Client::setContactRequestCallback(const OnContactRequestCallback& callback) {
  on_contact_request_ = std::make_shared<OnContactRequestCallback>(callback);
}
inline void Client::setContactStatusChangedCallback(const OnContactStatusChangedCallback& callback) {
  on_contact_status_changed_ = std::make_shared<OnContactStatusChangedCallback>(callback);
}
inline void Client::setMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback) {
  on_message_status_changed_ = std::make_shared<OnMessageStatusChangedCallback>(callback);
}
inline void Client::setPeerListUpdateCallback(const OnPeerListUpdateCallback& callback) {
  on_peer_list_update_ = std::make_shared<OnPeerListUpdateCallback>(callback);
}
inline void Client::setIncomingCallCallback(const OnIncomingCallCallback& callback) {
  on_incoming_call_ = std::make_shared<OnIncomingCallCallback>(callback);
}
inline void Client::requestPeerListUpdate() const {
  updateAndNotifyPeerList();
}
inline void Client::call(const std::string& peer_id) {
  connect(peer_id);
  message_history_->SetStatusChangedCallback([this](const std::string& p_id, std::uint64_t msg_id, messaging::MessageStatus status) {
    if (on_message_status_changed_) {
      (*on_message_status_changed_)(p_id, msg_id, status);
    }
  });
}
inline void Client::connect(const std::string& peer_id) {
  if (transport_->getPeer(peer_id)) {
    LOGW << "Already connected or connecting to peer " << peer_id;
    return;
  }
  const auto peer = std::make_shared<Peer>(peer_id);
  transport_->createDataPeerConnection(peer_id);
  setupDataChannel(peer, peer_id);
  transport_->addPeer(peer_id, peer);
}
inline void Client::startVoiceCall(const std::string& peer_id) {
  startMediaCall(peer_id, false);
}
inline void Client::startVideoCall(const std::string& peer_id) {
  startMediaCall(peer_id, true);
}
inline void Client::acceptCall(const std::string& peer_id) {
  auto peer = transport_->getPeer(peer_id);
  if (!peer || !peer->pending_offer_sdp) {
    LOGW << "Cannot accept call from " << peer_id << ", no pending offer found.";
    return;
  }
  transport_->createMediaPeerConnection(peer_id);
  av_stream_->Start();
  peer->audio_ = transport_->addAudio(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
    if (const auto p = wpeer.lock()) { transport_->addToStream(p, false); }
  });
  if (peer->pending_offer_sdp->find("m=video") != std::string::npos) {
    peer->video_ = transport_->addVideo(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
      if (const auto p = wpeer.lock()) { transport_->addToStream(p, true); }
    });
  }
  LOGI << "Accepting call from " << peer_id << ". Setting remote description.";
  transport_->setRemoteDescription(peer_id, "media", *peer->pending_offer_sdp, "offer");
  peer->pending_offer_sdp.reset();
  if (!peer->pending_candidates.empty()) {
    LOGI << "Applying " << peer->pending_candidates.size() << " buffered ICE candidates for " << peer_id;
    for (const auto& candidate : peer->pending_candidates) {
        transport_->addRemoteCandidate(peer_id, "media", candidate.sdp, candidate.mid);
    }
    peer->pending_candidates.clear();
  }
  LOGI << "Generating SDP answer for " << peer_id;
  transport_->setLocalDescription(peer_id, "media");
}
inline void Client::rejectCall(const std::string& peer_id) const {
  hangup(peer_id);
}
inline void Client::addContact(const std::string& peer_id) const {
  const auto existing_contact = contact_manager_->GetContact(peer_id);
  if (existing_contact && existing_contact->status == identity::ContactStatus::awaitingApproval) {
    confirmContact(peer_id);
    return;
  }
  if (contact_manager_->AddContact(peer_id)) {
    LOGI << "Queueing contact request for " << peer_id;
    const Json msg = {{"type", "contact_add_request"}, {"to", peer_id}};
    transport_->sendSignalingMessage(msg);
    if (on_contact_status_changed_) {
      (*on_contact_status_changed_)(peer_id, identity::ContactStatus::pendingApproval);
    }
  }
}
inline void Client::confirmContact(const std::string& peer_id) const {
  if (contact_manager_->ConfirmContact(peer_id)) {
    LOGI << "Sending contact confirmation for " << peer_id;
    const Json msg = {{"type", "contact_confirm"}, {"to", peer_id}};
    transport_->sendSignalingMessage(msg);
    if (on_contact_status_changed_) {
      (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
    }
  }
}
inline auto Client::getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState {
  return transport_->getPeerConnectionState(peer_id);
}
inline auto Client::getContacts() const -> std::vector<identity::Contact> {
  return contact_manager_->GetContacts();
}
inline auto Client::getChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary> {
  return message_history_->GetChatSummaries(peer_ids);
}
inline void Client::hangup(const std::string& peer_id) const {
  if (const auto peer = transport_->getPeer(peer_id); peer != nullptr) {
    if (peer->pending_offer_sdp) {
      peer->pending_offer_sdp.reset();
    }
    if (!peer->pending_candidates.empty()) {
      peer->pending_candidates.clear();
    }
    if (peer->has_media_peer_connection_) {
      LOGI << "Closing media peer connection for " << peer_id;
      transport_->closePeerConnection(peer_id, "media");
      peer->has_media_peer_connection_ = false;
      peer->audio_ = nullptr;
      peer->video_ = nullptr;
      av_stream_->Stop();
    } else {
      LOGW << "Hangup called for " << peer_id << ", but no media connection was found.";
    }
  }
}
inline void Client::configure(const Config& config) const {
  av_stream_->Video()->Configure(config);
  av_stream_->Audio()->Configure(config);
}
inline void Client::onPeerReady(const std::shared_ptr<Peer>&) {}
inline void Client::onPeerOnline(const std::string& peer_id) {
  LOGI << "Peer " << peer_id << " is online. Adding to list.";
  {
    std::lock_guard<std::mutex> lock(online_peers_mutex_);
    online_peers_[peer_id].id = peer_id;
    online_peers_[peer_id].name = peer_id;
  }
  updateAndNotifyPeerList();
  if (!message_queue_->GetQueuedMessages(peer_id).empty()) {
    delivery_service_->attemptDelivery(peer_id);
  }
}
inline void Client::onPeerOffline(const std::string& peer_id) {
  LOGI << "Peer " << peer_id << " is offline. Removing from list.";
  {
    std::lock_guard<std::mutex> lock(online_peers_mutex_);
    online_peers_.erase(peer_id);
  }
  updateAndNotifyPeerList();
}
inline void Client::onSignalingErrorForPeer(const std::string& peer_id, const std::string& error) {
  if (error == "peer_not_found") {
    LOGW << "Connection attempt failed for peer " << peer_id << " because they are not online. Will retry when they appear.";
    delivery_service_->connectionAttemptFinished(peer_id);
    if (transport_->getPeer(peer_id)) {
      transport_->closePeerConnection(peer_id, "data");
    }
  }
}
inline void Client::processPeerMessage(Json message) {
  const auto from_it = message.find("from");
  if (from_it == message.end()) {
    return;
  }
  auto peer_id = from_it->get<std::string>();
  const auto type_it = message.find("type");
  if (type_it == message.end()) {
    return;
  }
  auto type = type_it->get<std::string>();
  const auto context = message.value("context", "data");
  if (context == "media" && on_incoming_call_) {
    auto peer = transport_->getPeer(peer_id);
    if (!peer) {
      LOGI << "Received media message for unknown peer " << peer_id << ". Creating peer object first.";
      connect(peer_id);
      peer = transport_->getPeer(peer_id);
    }
    if (type == "offer") {
      LOGD << "Received media offer from " << peer_id << ". Storing it and notifying UI.";
      peer->pending_offer_sdp = message["sdp"].get<std::string>();
      if (on_incoming_call_) {
        auto call_type = (peer->pending_offer_sdp->find("m=video") != std::string::npos)
                             ? messaging::HistoryEntryKind::videoCall
                             : messaging::HistoryEntryKind::voiceCall;
        (*on_incoming_call_)(peer_id, call_type);
      }
    } else if (type == "candidate") {
      const auto candidate_str = message["candidate"].get<std::string>();
      const auto mid = message["sdpMid"].get<std::string>();
      if (peer->has_media_peer_connection_) {
        transport_->addRemoteCandidate(peer_id, "media", candidate_str, mid);
      } else {
        LOGD << "Buffering ICE candidate for peer " << peer_id;
        peer->pending_candidates.push_back({candidate_str, mid});
      }
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
  if (type == "offer") {
    auto peer = transport_->getPeer(peer_id);
    if (peer) {
      LOGW << "Glare detected on data channel for peer " << peer_id << ". Ignoring subsequent offer.";
      return;
    }
    peer = std::make_shared<Peer>(peer_id);
    transport_->createDataPeerConnection(peer_id);
    transport_->addPeer(peer_id, peer);
    const auto sdp = message["sdp"].get<std::string>();
    transport_->setRemoteDescription(peer_id, "data", sdp, "offer");
  } else if (type == "answer") {
    const auto sdp = message["sdp"].get<std::string>();
    transport_->setRemoteDescription(peer_id, "data", sdp, "answer");
  } else if (type == "candidate") {
    const auto candidate = message["candidate"].get<std::string>();
    const auto mid = message["sdpMid"].get<std::string>();
    transport_->addRemoteCandidate(peer_id, "data", candidate, mid);
  } else if (type == "contact_add_request") {
    if (contact_manager_->IsConfirmedContact(peer_id)) {
      LOGI << "Received contact request from already confirmed peer " << peer_id
           << ". Resending confirmation.";
      const Json msg = {{"type", "contact_confirm"}, {"to", peer_id}};
      transport_->sendSignalingMessage(msg);
    } else if (contact_manager_->ReceiveContactRequest(peer_id)) {
      if (on_contact_request_) {
        (*on_contact_request_)(peer_id);
      }
    }
  } else if (type == "contact_confirm") {
    if (contact_manager_->MarkAsConfirmed(peer_id)) {
      LOGI << "Contact " << peer_id << " has confirmed your request.";
      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
      }
    }
  }
}
inline void Client::processSystemMessage(const std::string& type, Json message) {
  if (type == "peers" && on_peer_list_update_) {
    std::vector<Device> peers;
    for (const auto& peer_json : message.at("peers")) {
      Device d;
      d.id = peer_json.at("id").get<std::string>();
      d.name = peer_json.value("name", d.id);
      peers.push_back(d);
    }
    (*on_peer_list_update_)(peers);
    return;
  }
  LOGW << "Unhandled system message of type: " << type;
}
inline void Client::onFrameReceived(const std::string& peer_id, const std::vector<std::byte>& frame) {
    if (on_frame_received_) {
        (*on_frame_received_)(frame);
    }
}
inline auto Client::sendChatMessage(const std::string& peer_id, const std::string& message) const -> messaging::HistoryEntryRecord {
  if (!contact_manager_->GetContact(peer_id)) {
    LOGE << "Cannot send message to " << peer_id.c_str() << ". Not a contact.";
    return {};
  }
  auto entry = message_history_->AddTextEntry(peer_id, message, messaging::MessageDirection::sent);
  const auto peer = transport_->getPeer(peer_id);
  if (peer && peer->data_channel_is_open_ && peer->session_cipher_->IsReady()) {
    if (directSend(peer, peer_id, message, entry.id)) {
      LOGI << "Sent direct message to " << peer_id;
      message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
      entry.status = messaging::MessageStatus::sent;
      return entry; // Success
    }
  }
  LOGI << "Peer " << peer_id << " is offline or not ready, or send failed. Buffering message.";
  const Json json_msg = {
      {"type", "chat"},
      {"content", message},
      {"message_id", entry.id},
  };
  message_queue_->Enqueue(peer_id, entry.id, json_msg.dump());
  if (!peer) {
    delivery_service_->attemptDelivery(peer_id);
  }
  return entry;
}
inline void Client::markMessagesAsRead(const std::string& peer_id, const std::vector<std::uint64_t>& message_ids) const {
  const auto peer = transport_->getPeer(peer_id);
  if (peer && peer->data_channel_is_open_) {
    const Json msg = {
        {"type", "message_read"},
        {"message_ids", message_ids},
    };
    transport_->sendPeerDataChannelMessage(peer_id, msg);
  }
}
inline auto Client::getMessageHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage {
  return message_history_->GetHistoryPage(peer_id, page_index, page_size);
}
inline void Client::startMediaCall(const std::string& peer_id, bool is_video) {
  av_stream_->Start();
  auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    LOGI << "Peer " << peer_id << " not found for call. Creating new peer object.";
    connect(peer_id);
    peer = transport_->getPeer(peer_id);
  }
  if (peer->has_media_peer_connection_) {
    LOGW << "Media connection already exists for peer " << peer_id << ", cannot start a new call.";
    av_stream_->Stop();
    return;
  }
  transport_->createMediaPeerConnection(peer_id);
  peer->has_media_peer_connection_ = true;
  const auto history_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;
  message_history_->AddCallEntry(peer_id, history_kind, messaging::CallDirection::outgoing, messaging::CallStatus::answered);
  peer->audio_ = transport_->addAudio(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
    if (const auto p = wpeer.lock()) { transport_->addToStream(p, false); }
  });
  if (is_video) {
    peer->video_ = transport_->addVideo(peer_id, [this, wpeer = make_weak_ptr(peer)]() {
      if (const auto p = wpeer.lock()) { transport_->addToStream(p, true); }
    });
  }
  transport_->setLocalDescription(peer_id, "media");
}
inline void Client::updateAndNotifyPeerList() const {
  if (on_peer_list_update_) {
    std::vector<Device> peer_list;
    {
      std::lock_guard<std::mutex> lock(online_peers_mutex_);
      for (const auto& pair : online_peers_) {
        peer_list.push_back(pair.second);
      }
    }
    (*on_peer_list_update_)(peer_list);
  }
}
inline auto Client::directSend(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::string& message, std::uint64_t message_id) const -> bool {
  if (!peer || !peer->data_channel_is_open_ || !peer->session_cipher_->IsReady()) {
    return false;
  }
  auto encrypted_payload = peer->session_cipher_->Encrypt(message);
  if (encrypted_payload.empty()) {
    LOGE << "Failed to encrypt chat message for peer " << peer_id;
    return false;
  }
  const Json json_message = {
      {"type", "chat"},
      {"message_id", message_id},
      {"payload", std::vector<std::byte>(encrypted_payload.begin(), encrypted_payload.end())}};
  transport_->sendPeerDataChannelMessage(peer_id, json_message);
  return true;
}
inline void Client::sendSampleToPeers(const std::vector<std::byte>& sample, Stream::Source::Type type, std::uint64_t sample_time) const {
  for (const auto& [id, peer] : transport_->getPeers()) {
    if (peer->GetState() == Peer::State::ready) {
      auto track = (type == Stream::Source::Type::video) ? peer->video_ : peer->audio_;
      if (track) {
        track->SendSample(sample, sample_time);
      }
    }
  }
}
inline void Client::processDataChannelMessage(const std::string& peer_id, const Json& message) {
  const auto itr = message.find("type");
  if (itr == message.end()) {
    return;
  }
  const auto type = itr->get<std::string>();
  const auto peer = transport_->getPeer(peer_id);
  if (!peer) {
    return;
  };
  if (type == "profile") {
    const auto profile = parseDeviceProfile(message.at("profile"));
    {
      const std::lock_guard<std::mutex> lock(online_peers_mutex_);
      online_peers_[peer_id].id = peer_id;
      online_peers_[peer_id].profile = profile;
      if (message.contains("name")) {
        online_peers_[peer_id].name = message.at("name").get<std::string>();
      }
    }
    updateAndNotifyPeerList();
    if (on_profile_received_) {
      (*on_profile_received_)(peer_id, profile);
    }
  } else if (type == "termination") {
    transport_->removePeer(peer_id);
  } else if (type == "key_exchange") {
    auto pub_key_bytes = message.at("key").get<std::vector<std::byte>>();
    if (peer->session_cipher_->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
      LOGI << "Successfully computed shared secret with peer " << peer_id;
      if (!message.value("is_reply", false)) {
        auto my_pub_key = peer->session_cipher_->GetPublicKey();
        const Json reply = {
            {"type", "key_exchange"},
            {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
            {"is_reply", true}};
    transport_->sendPeerDataChannelMessage(peer_id, reply);
      }
      sendQueuedMessages(peer_id);
    } else {
      LOGE << "Failed to compute shared secret with peer " << peer_id;
    }
  } else if (type == "chat") {
    if (on_chat_message_received_ && peer->session_cipher_->IsReady()) {
      auto payload = message.at("payload").get<std::vector<std::byte>>();
      auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
      if (!decrypted_message.empty()) {
        auto message_id = message.at("message_id").get<std::uint64_t>();
        const Json receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
        transport_->sendPeerDataChannelMessage(peer_id, receipt);
        auto history_entry = message_history_->AddTextEntry(peer_id, decrypted_message, messaging::MessageDirection::received);
        (*on_chat_message_received_)(peer_id, history_entry);
      } else {
        LOGW << "Failed to decrypt message from " << peer_id;
      }
    }
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
    auto message_ids = message.at("message_ids").get<std::vector<std::uint64_t>>();
    for (const auto& msg_id : message_ids) {
      const Json receipt = {
          {"type", "message_receipt"},
          {"status", "read"},
          {"message_id", msg_id},
      };
      transport_->sendPeerDataChannelMessage(peer_id, receipt);
    }
  } else {
    throw std::runtime_error("Unknown message type in ProcessDataChannelMessage");
  }
}
inline void Client::sendQueuedMessages(const std::string& peer_id) const {
  const auto messages = message_queue_->GetQueuedMessages(peer_id);
  if (messages.empty()) {
    return;
  }
  const auto peer = transport_->getPeer(peer_id);
  if (!peer || !peer->data_channel_is_open_) {
    LOGW << "sendQueuedMessages called but peer " << peer_id << " channel not ready. Messages will remain queued.";
    return;
  }
  LOGI << "Sending " << messages.size() << " queued messages to " << peer_id;
  for (const auto& msg : messages) {
    try {
      Json const message = Json::parse(msg.content);
      const auto type = message.value("type", "");
      if (type == "chat") {
        if (peer->session_cipher_->IsReady()) {
          if (!directSend(peer, peer_id, message.value("content", ""), message.value("message_id", 0ULL))) {
            LOGW << "Failed to send queued chat message to " << peer_id << ". It will remain in queue.";
            return;
          }
          message_history_->UpdateMessageStatus(peer_id, message.value("message_id", 0ULL), messaging::MessageStatus::sent);
        } else {
          LOGW << "Session cipher not ready for chat message to " << peer_id << ". Message will remain in queue.";
          return;
        }
      } else {
        transport_->sendPeerDataChannelMessage(peer_id, message);
      }
    } catch (const nlohmann::json::parse_error& e) {
      LOGE << "Failed to parse queued message: " << e.what() << ". Content: " << msg.content;
    }
  }
  message_queue_->Clear(peer_id);
}
inline void Client::setupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id) {
    transport_->createDataChannel(peer_id, [this, peer_id, peer](const std::string& msg){
        processDataChannelMessage(peer_id, nlohmann::json::parse(msg));
    });
}
inline auto Client::parseDeviceProfile(const Json& jprofile) -> Device::Profile {
  Device::Profile profile;
  for (const auto& cam_item : jprofile) {
    Device::Camera camera;
    camera.id = cam_item.at("id").get<std::string>();
    camera.name = cam_item.at("name").get<std::string>();
    for (const auto& res_item : cam_item.at("resolutions")) {
      camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
    }
    profile.push_back(camera);
  }
  return profile;
}

} // namespace eillance
