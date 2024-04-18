#pragma once

#include "logging/logging.h"
#include "utils/memory.h"

#include "nlohmann/json.hpp"
#include "rtc/rtc.h"

#include "core/rtc/peer.hpp"
#include "rtc_transport_base.hpp"
#include "core/rtc/stream.hpp"

#include <memory>


class RtcConsumer : public RtcTransportBase {
  using json = nlohmann::json;

  //	std::thread server_thread;
public:
  explicit RtcConsumer() = default;

  void setReceiver(const std::function<void(rtc::binary)>& receiver) {
    this->receiver = receiver;
  }

  void startClient() {
    start("client");
    //        server_thread = std::thread {[this]() {
    //            start("client");
    //        }};
    //        server_thread.detach();
    //        LOGI << "server_thread ready";
  }

  void requestStream() {
    // TODO
    LOGD << "Sending a stream request message";
    json message = {
        {"id", "server"},
        {"debug", "consumer.requestStream()"},
        {"type", "streamRequest"},
        {"receiver", "client"}};
    if (ws.isOpen()) {
      ws.send(message.dump());
    }
  }


protected:
  void wsOnMessage(json message) final {
    auto it = message.find("id");
    if (it == message.end()) {
      return;
    }
    std::string id = it->get<std::string>();
    it = message.find("type");
    if (it == message.end()) {
      return;
    }
    std::string type = it->get<std::string>();

    if (type == "offer") {
      auto sdp = message["sdp"].get<std::string>();
      auto offer = rtc::Description(sdp, type);
      std::shared_ptr<Peer> pc = createPeerConnection(id);
      pc->peerConnection->setRemoteDescription(offer);
      peers.emplace(id, pc);

      auto answer = pc->peerConnection->localDescription();
      json reply = {
          {"id", "server"},
          {"debug", "consumer.wsOnMessage(offer)"},
          {"type", answer->typeString()},
          {"sdp", std::string(answer.value())}};
      ws.send(reply.dump());
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void onPeerReady(const std::shared_ptr<Peer>& peer) final {}

  static auto addVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const rtc::Description::Direction direction,
      const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string& msid,
      const std::function<void(void)>& onOpen,
      const std::function<void(rtc::binary message)>& onMessage) -> std::shared_ptr<Peer::Track> {
    auto video = rtc::Description::Video(cname, direction);
    video.addH264Codec(payloadType);
    //        video.setBitrate(3000);

    auto track = pc->addTrack(video);
    auto session = std::make_shared<rtc::RtcpReceivingSession>();
    track->setMediaHandler(session);
    track->onOpen(onOpen);
    track->onMessage(onMessage, nullptr);

    return std::make_shared<Peer::Track>(track, session);
  }

  static auto addAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const rtc::Description::Direction direction,
      const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string& msid,
      const std::function<void(void)>& onOpen,
      const std::function<void(rtc::binary message)>& onMessage) -> std::shared_ptr<Peer::Track> {
    auto audio = rtc::Description::Audio(cname, direction);
    audio.addOpusCodec(payloadType);

    auto track = pc->addTrack(audio);
    auto session = std::make_shared<rtc::RtcpReceivingSession>();
    track->setMediaHandler(session);
    track->onOpen(onOpen);
    track->onMessage(onMessage, nullptr);

    return std::make_shared<Peer::Track>(track, session);
  }

  auto createPeerConnection(const std::string& id) -> std::shared_ptr<Peer> {
    auto pc = createDefaultPeerConnection(id);

    std::shared_ptr<Peer> peer(new Peer(pc));
    peer->video = addVideo(
        pc, rtc::Description::Direction::RecvOnly,
        102, 10001, "video-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              addToStream(c, true);
            }
          });
          LOGI << "Video from " << id << " opened";
        },
        [this](const rtc::binary& buffer) {
          if (likely(hasReceiver())) {
            (*this->receiver)(buffer);
          }
        });

    peer->audio = addAudio(
        pc, rtc::Description::Direction::RecvOnly,
        111, 10002, "audio-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              addToStream(c, false);
            }
          });
          LOGI << "Audio from " << id << " opened";
        },
        [](const rtc::binary& buffer) {
          LOGW << "AAAAAAAA";
        });

    peer->dataChannel = createDefaultDataChannel(pc, id);

    peer->setState(Peer::State::Ready);

    return peer;
  }


private:
  /// Receiver callback
  std::optional<std::function<void(rtc::binary)>> receiver = std::nullopt;

  auto hasReceiver() -> bool {
    return receiver.has_value();
  }
};
