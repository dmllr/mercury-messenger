#pragma once

#include <filesystem>
#include <memory>
#include <utility>

#include <nlohmann/json.hpp>
#include <rtc/rtc.h>

#include "eillance/capture/audio_capture.hpp"
#include "eillance/capture/capture.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/messaging/message_queue.hpp"
#include "eillance/rtc/peer.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/rtc/transport.hpp"
#include "eillance/storage/persistent_storage.hpp"
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

class RtcClient final : public RtcTransportDelegate {
  using Json = nlohmann::json;
  using PeerTrack = std::shared_ptr<Peer::Track>;
  using OnFrameReceivedCallback = std::function<void(const rtc::binary&)>;
  using OnProfileReceivedCallback = std::function<void(const std::string&, const Device::Profile&)>;
  using OnChatMessageReceivedCallback = std::function<void(const std::string&, const messaging::HistoryEntryRecord&)>;
  using OnContactRequestCallback = std::function<void(const std::string& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(const std::string& peer_id, identity::ContactStatus status)>;
  using OnMessageStatusChangedCallback = std::function<void(const std::string& peer_id, std::uint64_t message_id, messaging::MessageStatus status)>;
  using OnPeerListUpdateCallback = std::function<void(const std::vector<Device>&)>;

  std::shared_ptr<Stream> av_stream_;
  std::shared_ptr<RtcTransport> transport_;

  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  std::shared_ptr<OnChatMessageReceivedCallback> on_chat_message_received_{};
  std::shared_ptr<OnContactRequestCallback> on_contact_request_{};
  std::shared_ptr<OnContactStatusChangedCallback> on_contact_status_changed_{};
  std::shared_ptr<OnMessageStatusChangedCallback> on_message_status_changed_{};
  std::shared_ptr<OnPeerListUpdateCallback> on_peer_list_update_{};

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<identity::IdManager> id_manager_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<messaging::MessageQueue> message_queue_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;
  std::unique_ptr<messaging::MessageHistory> message_history_;

  std::unordered_map<std::string, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;

public:
  explicit RtcClient(std::shared_ptr<storage::PersistentStorage> storage, Capture video_capture)
      : storage_(std::move(storage)) {
    id_manager_ = std::make_unique<identity::IdManager>(storage_);
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
    message_queue_ = std::make_shared<messaging::MessageQueue>(storage_);
    message_history_ = std::make_unique<messaging::MessageHistory>(storage_);

    av_stream_ = std::make_shared<Stream>(
        std::make_shared<StreamSource>(std::move(video_capture)),
        std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

    transport_ = std::make_shared<RtcTransport>(this);

    delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(
        message_queue_,
        contact_manager_,
        [this](const std::string& peer_id) {
          // This handler is called only when a connection needs to be initiated.
          if (!transport_->GetPeer(peer_id)) {
            Connect(peer_id);
          }
          // If a peer object exists, a connection is already in progress.
          // Queued messages will be sent when the key exchange completes.
        });

    transport_->SetStatusCallback([this](const std::string& peer_id, const EventSource source, int state_val) {
      if (source == EventSource::peerConnectionState) {
        if (auto state = static_cast<rtc::PeerConnection::State>(state_val);
            state == rtc::PeerConnection::State::Disconnected ||
            state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Closed) {
          delivery_service_->connectionAttemptFinished(peer_id);
        }
      }
    });

    av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](const Stream::Source::Type type, std::uint64_t sample_time, const rtc::binary& sample) {
      SendSampleToPeers(sample, type, sample_time);
    });
  }

  RtcClient(RtcClient&&) = delete;
  RtcClient(const RtcClient&) = delete;
  RtcClient& operator=(RtcClient&&) = delete;
  RtcClient& operator=(const RtcClient&) = delete;
  ~RtcClient() override = default;

  [[nodiscard]] auto GetId() const -> std::string {
    return id_manager_->GetId();
  }

  auto GetStorage() const -> std::shared_ptr<storage::PersistentStorage> { return storage_; }

  void SetName(const std::string& name) const { transport_->SetName(name); }

  void Start() const {
    transport_->SetId(id_manager_->GetId());
    delivery_service_->start();
    transport_->Start();
  }

  void Terminate() const {
    delivery_service_->stop();
    transport_->Terminate();
  }
  void SetStatusCallback(const std::function<void(const std::string& peer_id, EventSource, int)>& callback) const { transport_->SetStatusCallback(callback); }
  void SetFrameReceivedCallback(const OnFrameReceivedCallback& callback) { on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback); }
  void SetProfileReceivedCallback(const OnProfileReceivedCallback& callback) { on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback); }
  void SetChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback) { on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback); }
  void SetContactRequestCallback(const OnContactRequestCallback& callback) { on_contact_request_ = std::make_shared<OnContactRequestCallback>(callback); }
  void SetContactStatusChangedCallback(const OnContactStatusChangedCallback& callback) { on_contact_status_changed_ = std::make_shared<OnContactStatusChangedCallback>(callback); }
  void SetMessageStatusChangedCallback(const OnMessageStatusChangedCallback& callback) { on_message_status_changed_ = std::make_shared<OnMessageStatusChangedCallback>(callback); }
  void SetPeerListUpdateCallback(const OnPeerListUpdateCallback& callback) { on_peer_list_update_ = std::make_shared<OnPeerListUpdateCallback>(callback); }

  void RequestPeerListUpdate() {
    UpdateAndNotifyPeerList();
  }

  void Call(const std::string& peer_id) {
    Connect(peer_id);
    message_history_->SetStatusChangedCallback([this](const std::string& p_id, std::uint64_t msg_id, messaging::MessageStatus status) {
      if (on_message_status_changed_) {
        (*on_message_status_changed_)(p_id, msg_id, status);
      }
    });
  }

  void Connect(const std::string& peer_id) {
    if (transport_->GetPeer(peer_id)) {
      LOGW << "Already connected or connecting to peer " << peer_id;
      return;
    }
    const auto peer = transport_->CreatePeer(peer_id);
    peer->data_channel_ = CreateDataChannel(peer, peer_id, "data");
    transport_->AddPeer(peer_id, peer);
    peer->peer_connection_->setLocalDescription();
  }

  void StartVoiceCall(const std::string& peer_id) {
    const auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      LOGW << "Cannot start voice call, not connected to peer " << peer_id;
      return;
    }
    if (!peer->audio_) {
      peer->audio_ = AddAudio(peer->peer_connection_, [this, wpeer = make_weak_ptr(peer)]() {
        if (const auto p = wpeer.lock()) {
          transport_->AddToStream(p, false);
        }
      });
      peer->peer_connection_->setLocalDescription();
    }
    message_history_->AddCallEntry(peer_id, messaging::HistoryEntryKind::VoiceCall, messaging::CallDirection::Outgoing, messaging::CallStatus::Answered); // Placeholder status
    av_stream_->Start();
  }

  void StartVideoCall(const std::string& peer_id) {
    const auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      LOGW << "Cannot start video call, not connected to peer " << peer_id;
      return;
    }
    if (!peer->video_) {
      peer->video_ = AddVideo(peer->peer_connection_, [this, wpeer = make_weak_ptr(peer)]() {
        if (const auto p = wpeer.lock()) {
          transport_->AddToStream(p, true);
        }
      });
      peer->peer_connection_->setLocalDescription();
    }
    message_history_->AddCallEntry(peer_id, messaging::HistoryEntryKind::VideoCall, messaging::CallDirection::Outgoing, messaging::CallStatus::Answered); // Placeholder status
    av_stream_->Start();
  }

  // Public methods for contact management
  void AddContact(const std::string& peer_id) const {
    // If a contact has already sent us a request, `/add` should act as an acceptance.
    const auto existing_contact = contact_manager_->GetContact(peer_id);
    if (existing_contact && existing_contact->status == identity::ContactStatus::awaitingApproval) {
      ConfirmContact(peer_id);
      return;
    }

    if (contact_manager_->AddContact(peer_id)) {
      LOGI << "Queueing contact request for " << peer_id;
      const Json msg = {{"type", "contact_add_request"}, {"to", peer_id}};
      transport_->SendSignalingMessage(msg);
      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::pendingApproval);
      }
    }
  }

  void ConfirmContact(const std::string& peer_id) const {
    if (contact_manager_->ConfirmContact(peer_id)) {
      LOGI << "Sending contact confirmation for " << peer_id;
      const Json msg = {{"type", "contact_confirm"}, {"to", peer_id}};
      transport_->SendSignalingMessage(msg);
      if (on_contact_status_changed_) {
        (*on_contact_status_changed_)(peer_id, identity::ContactStatus::confirmed);
      }
    }
  }

  auto GetContacts() const -> std::vector<identity::Contact> {
    return contact_manager_->GetContacts();
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) const -> std::vector<messaging::ChatSummary> {
    return message_history_->GetChatSummaries(peer_ids);
  }

  void Hangup(const std::string& peer_id) const {
    if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr) {
      if (peer->data_channel_ && peer->data_channel_->isOpen()) {
        const Json message = {{"type", "termination"}};
        transport_->SendPeerDataChannelMessage(peer->data_channel_, message);
      }
      peer->peer_connection_->close();
      transport_->RemovePeer(peer_id);
    }
  }

  void Configure(const Config& config) const {
    av_stream_->Video()->Configure(config);
    av_stream_->Audio()->Configure(config);
  }

  void OnPeerReady(const std::shared_ptr<Peer>&) override {}

  void OnPeerOnline(const std::string& peer_id) override {
    LOGI << "Peer " << peer_id << " is online. Adding to list.";
    {
      std::lock_guard<std::mutex> lock(online_peers_mutex_);
      online_peers_[peer_id].id = peer_id;
      online_peers_[peer_id].name = peer_id; // Use ID as placeholder for name
    }
    UpdateAndNotifyPeerList();

    if (!message_queue_->GetQueuedMessages(peer_id).empty()) {
      delivery_service_->attemptDelivery(peer_id);
    }
  }

  void OnPeerOffline(const std::string& peer_id) override {
    LOGI << "Peer " << peer_id << " is offline. Removing from list.";
    {
      std::lock_guard<std::mutex> lock(online_peers_mutex_);
      online_peers_.erase(peer_id);
    }
    UpdateAndNotifyPeerList();
  }

  void OnSignalingErrorForPeer(const std::string& peer_id, const std::string& error) override {
    if (error == "peer_not_found") {
      LOGW << "Connection attempt failed for peer " << peer_id << " because they are not online. Will retry when they appear.";
      // This is a critical step: we immediately mark the connection attempt as "finished"
      // so that the delivery service can try again when it receives an OnPeerOnline event.
      delivery_service_->connectionAttemptFinished(peer_id);

      // Also, the PeerConnection object created for this attempt is now useless,
      // so we should close and remove it.
      if (const auto peer = transport_->GetPeer(peer_id)) {
        peer->peer_connection_->close();
      }
    }
  }

  void ProcessPeerMessage(Json message) override {
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

    if (type == "offer") {
      // For now, we assume an offer implies a voice call, this can be refined by inspecting the SDP.
      message_history_->AddCallEntry(peer_id, messaging::HistoryEntryKind::VoiceCall, messaging::CallDirection::Incoming, messaging::CallStatus::Answered); // Placeholder
      std::shared_ptr<Peer> peer = transport_->GetPeer(peer_id);

      if (peer && peer->peer_connection_->signalingState() == rtc::PeerConnection::SignalingState::HaveLocalOffer) {
        // Glare condition detected. We have a local offer and received a remote offer.
        // We use lexicographical comparison of IDs as a tie-breaker.
        const bool am_i_polite = GetId() < peer_id;

        if (am_i_polite) {
          LOGW << "Glare detected, and our ID is smaller. We will be 'polite', "
               << "rolling back our local offer and accepting the remote one.";
          // To roll back, we destroy the current peer connection and create a new one.
          transport_->RemovePeer(peer_id);
          peer = nullptr; // Ensure we fall into the "new peer" logic below.
        } else {
          LOGW << "Glare detected, and our ID is larger. We will be 'impolite', "
               << "ignoring the remote offer and proceeding with ours.";
          return; // Ignore the incoming offer.
        }
      }

      if (!peer) {
        LOGI << "Received offer for new peer " << peer_id;
        peer = transport_->CreatePeer(peer_id);
        peer->peer_connection_->onDataChannel([this, peer_id, wpeer = std::weak_ptr(peer)](
            const std::shared_ptr<rtc::DataChannel>& data_channel) {
          if (auto p = wpeer.lock()) {
            if (data_channel->label() == "data") {
              SetupDataChannel(p, peer_id, data_channel);
            } else {
              LOGW << "Ignoring unexpected data channel with label: " << data_channel->label();
            }
          }
        });
        transport_->AddPeer(peer_id, peer);
      }
      const auto sdp = message["sdp"].get<std::string>();
      peer->peer_connection_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
    } else if (type == "answer") {
      if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr) {
        const auto sdp = message["sdp"].get<std::string>();
        const auto answer = rtc::Description(sdp, type);
        peer->peer_connection_->setRemoteDescription(answer);
      }
    } else if (type == "contact_add_request") {
      if (contact_manager_->ReceiveContactRequest(peer_id)) {
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

  void ProcessSystemMessage(const std::string& type, Json message) override {
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

  auto AddVideo(const std::shared_ptr<rtc::PeerConnection>& pc, const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> override {
    auto video = rtc::Description::Video("video", rtc::Description::Direction::SendOnly);
    constexpr auto payload_type = 102;
    constexpr auto ssrc = 10001;
    video.addH264Codec(payload_type);
    video.addSSRC(ssrc, "video-stream", "stream1", "video-label");

    auto track = pc->addTrack(video);

    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "video-stream", payload_type, rtc::H264RtpPacketizer::ClockRate);
    const auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtp_config);
    const auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    const auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, sr_reporter);
  }

  auto AddAudio(const std::shared_ptr<rtc::PeerConnection>& pc, const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> override {
    auto audio = rtc::Description::Audio("audio", rtc::Description::Direction::SendOnly);
    constexpr auto payload_type = 111;
    constexpr auto ssrc = 10002;
    audio.addOpusCodec(payload_type);
    audio.addSSRC(ssrc, "audio-stream", "stream1", "audio-label");
    auto track = pc->addTrack(audio);

    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "audio-stream", payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
    const auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtp_config);
    const auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    const auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, sr_reporter);
  }

  void OnTrack(const std::string&, const std::shared_ptr<rtc::Track> track) override {
    if (track->mid().find("video") != std::string::npos) {
      const auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
      track->setMediaHandler(depacketizer);
      track->onFrame([this](const rtc::binary& data, rtc::FrameInfo) {
        if (on_frame_received_) {
          (*on_frame_received_)(data);
        }
      });
    }
  }

  auto SendChatMessage(const std::string& peer_id, const std::string& message) const -> messaging::HistoryEntryRecord {
    if (!contact_manager_->GetContact(peer_id)) {
      LOGE << "Cannot send message to " << peer_id.c_str() << ". Not a contact.";
      return {};
    }

    auto entry = message_history_->AddTextEntry(peer_id, message, messaging::MessageDirection::sent);

    const auto peer = transport_->GetPeer(peer_id);
    if (peer && peer->data_channel_ && peer->data_channel_->isOpen() && peer->session_cipher_->IsReady()) {
      if (DirectSend(peer, peer_id, message, entry.id)) {
        LOGI << "Sent direct message to " << peer_id;
        message_history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::Sent);
        entry.status = messaging::MessageStatus::Sent;
        return entry; // Success
      }
    }

    // If we reach here, direct send failed or was not possible.
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

  void MarkMessagesAsRead(const std::string& peer_id, const std::vector<std::uint64_t>& message_ids) const {
      const auto peer = transport_->GetPeer(peer_id);
      if (peer && peer->data_channel_ && peer->data_channel_->isOpen()) {
          const Json msg = {
            {"type", "message_read"},
            {"message_ids", message_ids},
          };
          transport_->SendPeerDataChannelMessage(peer->data_channel_, msg);
      }
  }

  auto GetMessageHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const -> messaging::HistoryPage {
    return message_history_->GetHistoryPage(peer_id, page_index, page_size);
  }

private:
  void UpdateAndNotifyPeerList() const {
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

  auto DirectSend(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::string& message, std::uint64_t message_id) const -> bool {
    if (!peer || !peer->data_channel_ || !peer->data_channel_->isOpen() || !peer->session_cipher_->IsReady()) {
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
        {"payload", rtc::binary(encrypted_payload.begin(), encrypted_payload.end())}};
    transport_->SendPeerDataChannelMessage(peer->data_channel_, json_message);
    return true;
  }

  void SendSampleToPeers(const rtc::binary& sample, Stream::Source::Type type, std::uint64_t sample_time) const {
    for (const auto& [id, peer]: transport_->GetPeers()) {
      if (peer->GetState() == Peer::State::ready) {
        auto track = (type == Stream::Source::Type::video) ? peer->video_ : peer->audio_;
        if (track) {
          const auto elapsed_seconds = static_cast<double>(sample_time) / Time::uS;
          const auto rtp_config = track->Reporter()->rtpConfig;
          const auto elapsed_timestamp = rtp_config->secondsToTimestamp(elapsed_seconds);
          rtp_config->timestamp = rtp_config->startTimestamp + elapsed_timestamp;

          if (!sample.empty()) {
            track->Send(sample);
          }
        }
      }
    }
  }

  void ProcessDataChannelMessage(const std::string& peer_id, const Json& message) {
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    const auto type = itr->get<std::string>();
    const auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    };

    if (type == "profile") {
      const auto profile = ParseDeviceProfile(message.at("profile"));
      {
        const std::lock_guard<std::mutex> lock(online_peers_mutex_);
        online_peers_[peer_id].id = peer_id;
        online_peers_[peer_id].profile = profile;
        if (message.contains("name")) {
          online_peers_[peer_id].name = message.at("name").get<std::string>();
        }
      }
      UpdateAndNotifyPeerList();

      if (on_profile_received_) {
        (*on_profile_received_)(peer_id, profile);
      }
    } else if (type == "termination") {
      transport_->RemovePeer(peer_id);
    } else if (type == "key_exchange") {
      auto pub_key_bytes = message.at("key").get<rtc::binary>();
      if (peer->session_cipher_->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
        LOGI << "Successfully computed shared secret with peer " << peer_id;
        if (!message.value("is_reply", false)) {
          auto my_pub_key = peer->session_cipher_->GetPublicKey();
          const Json reply = {
              {"type", "key_exchange"},
              {"key", rtc::binary(my_pub_key.begin(), my_pub_key.end())},
              {"is_reply", true}};
          transport_->SendPeerDataChannelMessage(peer->data_channel_, reply);
        }
        // Key exchange is complete, now we can send queued messages.
        SendQueuedMessages(peer_id);
      } else {
        LOGE << "Failed to compute shared secret with peer " << peer_id;
      }
    } else if (type == "chat") {
      if (on_chat_message_received_ && peer->session_cipher_->IsReady()) {
        auto payload = message.at("payload").get<rtc::binary>();
        auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
        if (!decrypted_message.empty()) {
          auto message_id = message.at("message_id").get<std::uint64_t>();
          const Json receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
          transport_->SendPeerDataChannelMessage(peer->data_channel_, receipt);

          auto history_entry = message_history_->AddTextEntry(peer_id, decrypted_message, messaging::MessageDirection::received);
          (*on_chat_message_received_)(peer_id, history_entry);
        } else {
          LOGW << "Failed to decrypt message from " << peer_id;
        }
      }
    } else if (type == "message_receipt") {
        auto message_id = message.at("message_id").get<std::uint64_t>();
        auto status_str = message.at("status").get<std::string>();
        auto status = messaging::MessageStatus::Sent;
        if (status_str == "delivered") {
            status = messaging::MessageStatus::Delivered;
        } else if (status_str == "read") {
            status = messaging::MessageStatus::Read;
        }
        message_history_->UpdateMessageStatus(peer_id, message_id, status);
    } else if (type == "message_read") {
        auto message_ids = message.at("message_ids").get<std::vector<std::uint64_t>>();
        for(const auto& msg_id : message_ids) {
            // We don't update our own history; the sender will be notified via receipt.
            // This is just for sending the receipt back.
            const Json receipt = {
              {"type", "message_receipt"},
              {"status", "read"},
              {"message_id", msg_id},
            };
            transport_->SendPeerDataChannelMessage(peer->data_channel_, receipt);
        }
    } else {
      throw std::runtime_error("Unknown message type in ProcessDataChannelMessage");
    }
  }

  void SendQueuedMessages(const std::string& peer_id) const {
    const auto messages = message_queue_->GetQueuedMessages(peer_id);
    if (messages.empty()) {
      return;
    }

    const auto peer = transport_->GetPeer(peer_id);
    if (!peer || !peer->data_channel_ || !peer->data_channel_->isOpen()) {
      LOGW << "SendQueuedMessages called but peer " << peer_id << " channel not ready. Messages will remain queued.";
      return;
    }

    LOGI << "Sending " << messages.size() << " queued messages to " << peer_id;
    for (const auto& msg: messages) {
      try {
        Json const message = Json::parse(msg.content);
        const auto type = message.value("type", "");

        if (type == "chat") {
          if (peer->session_cipher_->IsReady()) {
            if (!DirectSend(peer, peer_id, message.value("content", ""), message.value("message_id", 0ULL))) {
              LOGW << "Failed to send queued chat message to " << peer_id << ". It will remain in queue.";
              return;  // Abort and retry later
            }
            message_history_->UpdateMessageStatus(peer_id, message.value("message_id", 0ULL), messaging::MessageStatus::Sent);
          } else {
            LOGW << "Session cipher not ready for chat message to " << peer_id << ". Message will remain in queue.";
            return;
          }
        } else {
          transport_->SendPeerDataChannelMessage(peer->data_channel_, message);
        }
      } catch (const nlohmann::json::parse_error& e) {
        LOGE << "Failed to parse queued message: " << e.what() << ". Content: " << msg.content;
      }
    }
    // All messages sent successfully, clear the queue.
    message_queue_->Clear(peer_id);
  }

  void SetupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::shared_ptr<rtc::DataChannel>& data_channel) {
    peer->data_channel_ = data_channel;
    data_channel->onOpen([this, peer_id, peer]() {
      LOGI << "Data channel with " << peer_id << " opened.";
      delivery_service_->connectionAttemptFinished(peer_id);

      // Initiate the key exchange
      auto my_pub_key = peer->session_cipher_->GetPublicKey();
      const Json key_exchange_message = {
          {"type", "key_exchange"},
          {"key", rtc::binary(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}};
      transport_->SendPeerDataChannelMessage(peer->data_channel_, key_exchange_message);
    });
    data_channel->onMessage(nullptr, [this, peer_id](const std::string& msg) {
      try {
        ProcessDataChannelMessage(peer_id, Json::parse(msg));
      } catch (const nlohmann::json::parse_error& e) {
        LOGE << "Failed to parse data channel message: " << e.what();
      }
    });
  }

  auto CreateDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::string& label) -> std::shared_ptr<rtc::DataChannel> {
    auto data_channel = peer->peer_connection_->createDataChannel(label);
    SetupDataChannel(peer, peer_id, data_channel);
    return data_channel;
  }

  static auto ParseDeviceProfile(const Json& jprofile) -> Device::Profile {
    Device::Profile profile;
    for (const auto& cam_item: jprofile) {
      Device::Camera camera;
      camera.id = cam_item.at("id").get<std::string>();
      camera.name = cam_item.at("name").get<std::string>();
      for (const auto& res_item: cam_item.at("resolutions")) {
        camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
      }
      profile.push_back(camera);
    }
    return profile;
  }

};

}  // namespace eillance
