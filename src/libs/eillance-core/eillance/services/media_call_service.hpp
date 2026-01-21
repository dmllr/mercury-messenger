#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "eillance/capture/capture.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

class MediaCallService {
 public:
  explicit MediaCallService(std::shared_ptr<transport::Transport> transport);

  void SetMessagingService(MessagingService* messaging_service);

  // Media Capture Injection
  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    video_capture_ = std::make_unique<Capture>(std::move(impl));
    if (audio_capture_) InitializeAvStream();
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    audio_capture_ = std::make_unique<Capture>(std::move(impl));
    if (video_capture_) InitializeAvStream();
  }

  void Configure(Config const& config);

  // Actions
  void StartVoiceCall(ContactID const& peer_id);
  void StartVideoCall(ContactID const& peer_id);
  void AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind);
  void RejectCall(ContactID const& peer_id);
  void Hangup(ContactID const& peer_id, bool notify_peer);

  // Incoming Events
  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame);
  void OnCallRequest(ContactID const& peer_id, messaging::HistoryEntryKind kind);
  void OnCallRinging(ContactID const& peer_id);
  void OnCallAccepted(ContactID const& peer_id, messaging::HistoryEntryKind kind);

  // Transport signals
  void OnMediaPeerConnectionStateChange(ContactID const& peer_id, transport::PeerConnectionState state);

  // Callbacks
  using OnIncomingCallCallback = std::function<void(ContactID const&, messaging::HistoryEntryKind)>;
  using OnCallRingingCallback = std::function<void(ContactID const&)>;
  using OnCallAcceptedCallback = std::function<void(ContactID const&)>;
  using OnCallTerminatedCallback = std::function<void(ContactID const&)>;
  using OnFrameReceivedCallback = std::function<void(std::vector<std::byte> const&)>;

  void SetIncomingCallCallback(OnIncomingCallCallback cb);
  void SetCallRingingCallback(OnCallRingingCallback cb);
  void SetCallAcceptedCallback(OnCallAcceptedCallback cb);
  void SetCallTerminatedCallback(OnCallTerminatedCallback cb);
  void SetFrameReceivedCallback(OnFrameReceivedCallback cb);

 private:
  void InitializeAvStream();
  void StartCallInternal(ContactID const& peer_id, bool is_video);
  void SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type type, uint64_t time) const;

  std::shared_ptr<transport::Transport> transport_;
  MessagingService* messaging_service_{nullptr};

  std::unique_ptr<Capture> video_capture_;
  std::unique_ptr<Capture> audio_capture_;
  std::shared_ptr<Stream> av_stream_;

  OnIncomingCallCallback on_incoming_call_;
  OnCallRingingCallback on_call_ringing_;
  OnCallAcceptedCallback on_call_accepted_;
  OnCallTerminatedCallback on_call_terminated_;
  OnFrameReceivedCallback on_frame_received_;
};

}  // namespace eillance::services
