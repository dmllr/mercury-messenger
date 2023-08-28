#pragma once

#include "logging/logging.h"
#include "memory.h"

#include "rtc/rtc.h"
#include "nlohmann/json.hpp"

#include "peer.hpp"
#include "stream.hpp"
#include "rtc_transport_base.hpp"

#include <memory>
#include <thread>
#include <unordered_map>


class RtcProducer : public RtcTransportBase {
	using json = nlohmann::json;

	std::thread server_thread;
public:
	explicit RtcProducer() = default;

	void setStream(const std::shared_ptr<Stream>& stream) {
		avStream = stream;

		// set callback responsible for sample sending
		stream->onSample([this, ws = make_weak_ptr(stream)](Stream::StreamSourceType type, uint64_t sampleTime, rtc::binary sample) {
			struct ClientTrack {
				std::string id;
				std::shared_ptr<Peer::Track> trackData;

				ClientTrack(const std::string& id, std::shared_ptr<Peer::Track> trackData) {
					this->id = id;
					this->trackData = std::move(trackData);
				}
			};

			auto tracks{std::vector<ClientTrack>()};
			auto streamType = type == Stream::StreamSourceType::Video ? "video" : "audio";
			// get track for given type
			auto getTrackData =
					[type](const std::shared_ptr<Peer>& peer) {
						return type == Stream::StreamSourceType::Video ? peer->video : peer->audio;
					};

			// get all peers with Ready state
			for (const auto& id_peer: peers) {
				auto id = id_peer.first;
				auto peer = id_peer.second;
				auto optTrackData = getTrackData(peer);
				if (peer->getState() == Peer::State::Ready && optTrackData.has_value()) {
					auto trackData = optTrackData.value();
					tracks.emplace_back(id, trackData);
				}
			}

			if (!tracks.empty()) {
				auto message = rtc::make_message(std::move(sample));
				for (const auto& clientTrack: tracks) {
					auto peer = clientTrack.id;
					auto trackData = clientTrack.trackData;
					// sample time is in us, we need to convert it to seconds
					auto elapsedSeconds = double(sampleTime) / (1000 * 1000);
					auto rtpConfig = trackData->sender->rtpConfig;
					// get elapsed time in clock rate
					uint32_t elapsedTimestamp = rtpConfig->secondsToTimestamp(elapsedSeconds);

					// set new timestamp
					rtpConfig->timestamp = rtpConfig->startTimestamp + elapsedTimestamp;

					// get elapsed time in clock rate from last RTCP sender report
					auto reportElapsedTimestamp = rtpConfig->timestamp - trackData->sender->lastReportedTimestamp();
					// check if last report was at least 1 second ago
					if (rtpConfig->timestampToSeconds(reportElapsedTimestamp) > 1) {
						trackData->sender->setNeedsToReport();
					}
					LOGV << "Sending " << streamType << " sample with size: " << message->size() << " to " << peer;
					bool send = false;
					try {
						// send sample
						if (likely(message->size()))
							send = trackData->track->send(*message);
					} catch (...) {
						send = false;
					}
					if (!send) {
						LOGE << "Unable to send " << streamType << " packet";
						break;
					}
				}
			}

			MainThread.dispatch([this, ws]() {
				if (peers.empty()) {
					// we have no peers, stop the stream
					if (auto stream = ws.lock()) {
						stream->stop();
					}
				}
			});
		});
	}

    void startServer() {
        server_thread = std::thread {[this]() {
            start("server");
        }};
        server_thread.detach();
        LOGI << "server_thread ready";
    }


protected:

	void wsOnMessage(json message) final {
		auto it = message.find("id");
		if (it == message.end())
			return;
		std::string id = it->get<std::string>();
		it = message.find("type");
		if (it == message.end())
			return;
		std::string type = it->get<std::string>();

		if (type == "streamRequest") {
            std::shared_ptr<Peer> c = createPeerConnection(id);
            peers.emplace(id, c);
        } else if (type == "answer") {
			if (auto jt = peers.find(id); jt != peers.end()) {
				auto pc = peers.at(id)->peerConnection;
				auto sdp = message["sdp"].get<std::string>();
				auto answer = rtc::Description(sdp, type);
				pc->setRemoteDescription(answer);
			}
        } else if (type == "offer") {
            // do nothing
		} else {
			throw std::runtime_error("Unknown message type " + type);
		}
	}

    void onPeerReady(const std::shared_ptr<Peer>& peer) final {
        if (hasStream()) {
            startStream();
        }
    }

    std::shared_ptr<Peer> createPeerConnection(const std::string& id) {
        auto pc = createDefaultPeerConnection(id);

        std::shared_ptr<Peer> peer(new Peer(pc));

        peer->video = addVideo(
            pc, rtc::Description::Direction::SendOnly,
            102, 10001, "video-stream", "cam-stream-1",
            [this, id, wc = make_weak_ptr(peer)]() {
                MainThread.dispatch([this, wc]() {
                    if (auto c = wc.lock()) {
                        addToStream(c, true);
                    }
                });
                LOGI << "Video from " << id << " opened";
            },
            nullptr
        );

        peer->audio = addAudio(
            pc, rtc::Description::Direction::SendOnly,
            111, 10002, "audio-stream", "cam-stream-1",
            [this, id, wc = make_weak_ptr(peer)]() {
                MainThread.dispatch([this, wc]() {
                    if (auto c = wc.lock()) {
                        addToStream(c, false);
                    }
                });
                LOGI << "Audio from " << id << " opened";
            },
            nullptr
        );

        peer->dataChannel = createDefaultDataChannel(pc, id);

        pc->setLocalDescription();

        return peer;
    }

	/// Start stream
	void startStream() {
		if (unlikely(!avStream.has_value())) {
			LOGE << "Stream is not set";
		}

		avStream.value()->start();
	}

private:
	/// Audio and video stream
	std::optional<std::shared_ptr<Stream>> avStream = std::nullopt;

    bool hasStream() {
        return avStream.has_value();
    }
};
