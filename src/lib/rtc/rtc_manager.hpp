#pragma once

#include "logging/logging.h"

#include "rtc/rtc.h"
#include "nlohmann/json.hpp"
#include "helpers.hpp"
#include "dispatch_queue.hpp"

#ifdef ANDROID
// for initial NALUs
//#include "camera_buffer_source.hpp"
#endif
#include "stream.hpp"

#include <memory>
#include <thread>
#include <unordered_map>


DispatchQueue MainThread("Main", 1);
template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }


class RtcManager {
	using json = nlohmann::json;

	std::thread server_thread;
public:
	explicit RtcManager() = default;

	void setStream(const std::shared_ptr<Stream>& stream) {
		avStream = stream;

		// set callback responsible for sample sending
		stream->onSample([this, ws = make_weak_ptr(stream)](Stream::StreamSourceType type, uint64_t sampleTime, rtc::binary sample) {
			auto tracks{std::vector<ClientTrack>()};
			auto streamType = type == Stream::StreamSourceType::Video ? "video" : "audio";
			// get track for given type
			auto getTrackData =
					[type](const std::shared_ptr<Connection>& client) {
						return type == Stream::StreamSourceType::Video ? client->video : client->audio;
					};

			// get all clients with Ready state
			for (const auto& id_client: clients) {
				auto id = id_client.first;
				auto client = id_client.second;
				auto optTrackData = getTrackData(client);
				if (client->getState() == Connection::State::Ready && optTrackData.has_value()) {
					auto trackData = optTrackData.value();
					tracks.emplace_back(id, trackData);
				}
			}

			if (!tracks.empty()) {
				auto message = rtc::make_message(std::move(sample));
				for (const auto& clientTrack: tracks) {
					auto client = clientTrack.id;
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
					LOGV << "Sending " << streamType << " sample with size: " << message->size() << " to " << client;
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
				if (clients.empty()) {
					LOGI << "clients.empty(): true";
					// we have no clients, stop the stream
					if (auto stream = ws.lock()) {
						LOGI << "stream->stop() now";
						stream->stop();
					}
				} else {
					for (auto& c : clients) {
						LOGI << "client: " << c.first << (int)c.second->getState();
					}
				}
			});
		});
	}

    void setReceiver(std::function<void(rtc::binary)> receiver) {
        this->receiver = receiver;
    }

    void startServer() {
        server_thread = std::thread {[this]() {
            start("server");
        }};
        server_thread.detach();
        LOGI << "server_thread ready";
    }

    void startClient() {
        server_thread = std::thread {[this]() {
            start("client");
        }};
        server_thread.detach();
        LOGI << "server_thread ready";
    }

	void reload() {
		if (hasReceiver()) {
			requestStream();
		}
	}


protected:

	void start(std::string_view localId) {
		try {
			rtc::Configuration config;
			config.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80");
			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80?transport=tcp");
			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443?transport=tcp");

			LOGI << "The local ID is: " << localId.data();

			ws = std::make_shared<rtc::WebSocket>();

			ws->onOpen([this]() {
				LOGI << "WebSocket connected, signaling ready";

                reload();
			});

			ws->onClosed([]() {
				LOGI << "WebSocket closed";
			});

			ws->onError([](const std::string& error) {
				LOGI << "WebSocket failed: " << error.c_str();
			});

			ws->onMessage([&](std::variant<rtc::binary, std::string> data) {
				if (!std::holds_alternative<std::string>(data))
					return;

				json message = json::parse(std::get<std::string>(data));
				MainThread.dispatch([this, message, config]() {
					wsOnMessage(message, config);
				});
			});

			const std::string url = "wss://" + ip_address + ":" + std::to_string(port) + "/" + localId.data();
			LOGI << "Url is " << url.c_str();
			ws->open(url);

			LOGI << "Waiting for signaling to be connected...";
			while (!ws->isOpen()) {
				if (ws->isClosed())
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				// TODO Stop option
			}

			LOGI << "Cleaning up...";
			return;
		} catch (const std::exception& e) {
			LOGI << "Error: " << e.what();
			return;
		}
	}

	void wsOnMessage(json message, const rtc::Configuration& config) {
		auto it = message.find("id");
		if (it == message.end())
			return;
		std::string id = it->get<std::string>();
		it = message.find("type");
		if (it == message.end())
			return;
		std::string type = it->get<std::string>();

		if (type == "streamRequest") {
            std::shared_ptr<Connection> c = createSendingPeerConnection(config, make_weak_ptr(ws), id);
            clients.emplace(id, c);
        } else if (type == "answer") {
			if (auto jt = clients.find(id); jt != clients.end()) {
				auto pc = clients.at(id)->peerConnection;
				auto sdp = message["sdp"].get<std::string>();
				auto description = rtc::Description(sdp, type);
				pc->setRemoteDescription(description);
			}
        } else if (type == "offer") {
            std::shared_ptr<Connection> pc = createReceivingPeerConnection(config, make_weak_ptr(ws), id);
            clients.emplace(id, pc);
            auto sdp = message["sdp"].get<std::string>();
            auto description = rtc::Description(sdp, type);
            pc->peerConnection->setRemoteDescription(description);
        }
	}

    void requestStream() {
		// TODO
        LOGD << "Sending a stream request message";
        ws->send("{\n"
                 "  \"id\": \"server\",\n"
                 "  \"type\": \"streamRequest\",\n"
                 "  \"receiver\": \"client\"\n"
                 "}");
    }

    std::shared_ptr<Connection> createSendingPeerConnection(const rtc::Configuration& config, std::weak_ptr<rtc::WebSocket> wws, const std::string& id) {
        auto pc = createDefaultPeerConnection(config, wws, id);
        std::shared_ptr<Connection> client(new Connection(pc));

        client->video = addVideo(pc, 102, 10001, "video-stream", "cam-stream-1", [this, id, wc = make_weak_ptr(client)]() {
            MainThread.dispatch([this, wc]() {
                if (auto c = wc.lock()) {
                    addToStream(c, true);
                }
            });
            LOGI << "Video from " << id << " opened";
        });

        client->audio = addAudio(pc, 111, 10002, "audio-stream", "cam-stream-1", [this, id, wc = make_weak_ptr(client)]() {
            MainThread.dispatch([this, wc]() {
                if (auto c = wc.lock()) {
                    addToStream(c, false);
                }
            });
            LOGI << "Audio from " << id << " opened";
        });

        client->dataChannel = createDefaultDataChannel(pc, id);

        pc->setLocalDescription();

        return client;
    }

    std::shared_ptr<Connection> createReceivingPeerConnection(const rtc::Configuration& config, std::weak_ptr<rtc::WebSocket> wws, const std::string& id) {
        auto pc = createDefaultPeerConnection(config, wws, id);
        std::shared_ptr<Connection> client(new Connection(pc));

        rtc::Description::Video media("video", rtc::Description::Direction::RecvOnly);
        media.addH264Codec(96);
//        media.addH264Codec(102);
        media.setBitrate(3000); // Request 3Mbps (Browsers do not encode more than 2.5MBps from a webcam)

        auto track = pc->addTrack(media);

        auto session = std::make_shared<rtc::RtcpReceivingSession>();
        track->setMediaHandler(session);

        track->onMessage(
                [](rtc::binary message) {
                    // received is an RTP packet
                    LOGW << "Yes. " << message.size() << std::endl;
                },
				nullptr
        );

        client->dataChannel = createDefaultDataChannel(pc, id);

        pc->setLocalDescription();

        return client;
    }

    std::shared_ptr<rtc::PeerConnection> createDefaultPeerConnection(const rtc::Configuration& config, std::weak_ptr<rtc::WebSocket> wws, const std::string& id) {
        auto pc = std::make_shared<rtc::PeerConnection>(config);

        pc->onStateChange([this, id](rtc::PeerConnection::State state) {
            LOGI << "State: " << (int)state;
            if (state == rtc::PeerConnection::State::Disconnected ||
                state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                // remove disconnected client
                LOGI << "clients.erase(" << id << "); ";
                MainThread.dispatch([this, id]() {
                    LOGI << "main: clients.erase(" << id << "); ";
                    clients.erase(id);
                });
            }
        });

        pc->onGatheringStateChange(
                [wpc = make_weak_ptr(pc), id, wws](rtc::PeerConnection::GatheringState state) {
                    LOGI << "Gathering State: " << (int)state;
                    if (state == rtc::PeerConnection::GatheringState::Complete) {
                        if(auto pc = wpc.lock()) {
                            auto description = pc->localDescription();
                            json message = {
                                    {"id", id},
                                    {"type", description->typeString()},
                                    {"sdp", std::string(description.value())}
                            };
                            // Gathering complete, send answer
                            if (auto ws = wws.lock()) {
                                ws->send(message.dump());
                            }
                        }
                    }
                });

        return pc;
    }

    static std::shared_ptr<rtc::DataChannel> createDefaultDataChannel(std::shared_ptr<rtc::PeerConnection> pc, const std::string& id) {
        auto dc = pc->createDataChannel("ping-pong");
        dc->onOpen([id, wdc = make_weak_ptr(dc)]() {
            if (auto dc = wdc.lock()) {
                dc->send("Ping");
            }
        });

        dc->onMessage(nullptr, [id, wdc = make_weak_ptr(dc)](const std::string& msg) {
            LOGI << "Message from " << id << " received: " << msg;
            if (auto dc = wdc.lock()) {
                dc->send("Ping");
            }
        });

        return dc;
    }

	void sendInitialNalus(std::shared_ptr<Stream> stream, std::shared_ptr<Connection::Track> video) {
//		auto h264 = dynamic_cast<CameraBufferSource*>(stream->video.get());
//		auto initialNalus = h264->initialNALUS();
//
//		// send previous NALU key frame so users don't have to wait to see stream works
//		if (!initialNalus.empty()) {
//			const double frameDuration_s = double(h264->sampleDuration_us) / (1000 * 1000);
//			const uint32_t frameTimestampDuration = video->sender->rtpConfig->secondsToTimestamp(frameDuration_s);
//			video->sender->rtpConfig->timestamp = video->sender->rtpConfig->startTimestamp - frameTimestampDuration * 2;
//			video->track->send(initialNalus);
//			video->sender->rtpConfig->timestamp += frameTimestampDuration;
//			// Send initial NAL units again to start stream in firefox browser
//			video->track->send(initialNalus);
//		}
	}

	static std::shared_ptr<Connection::Track> addVideo(const std::shared_ptr<rtc::PeerConnection>& pc, const uint8_t payloadType, const uint32_t ssrc, const std::string cname, const std::string msid, const std::function<void (void)> onOpen) {
		auto video = rtc::Description::Video(cname);
		video.addH264Codec(payloadType);
		video.addSSRC(ssrc, cname, msid, cname);
		auto track = pc->addTrack(video);
		// create RTP configuration
		auto rtpConfig = std::shared_ptr<rtc::RtpPacketizationConfig>(new rtc::RtpPacketizationConfig(ssrc, cname, payloadType, rtc::H264RtpPacketizer::defaultClockRate));
		// create packetizer
		auto packetizer = std::shared_ptr<rtc::H264RtpPacketizer>(new rtc::H264RtpPacketizer(rtc::H264RtpPacketizer::Separator::StartSequence, rtpConfig));
		// create H264 handler
		std::shared_ptr<rtc::H264PacketizationHandler> h264Handler(new rtc::H264PacketizationHandler(packetizer));
		// add RTCP SR handler
		auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
		h264Handler->addToChain(srReporter);
		// add RTCP NACK handler
		auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
		h264Handler->addToChain(nackResponder);
		// set handler
		track->setRtcpHandler(h264Handler);
		track->onOpen(onOpen);
		auto trackData = std::make_shared<Connection::Track>(track, srReporter);

		return trackData;
	}

	static std::shared_ptr<Connection::Track> addAudio(const std::shared_ptr<rtc::PeerConnection>& pc, const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string msid, const std::function<void (void)> onOpen) {
		auto audio = rtc::Description::Audio(cname);
		audio.addOpusCodec(payloadType);
		audio.addSSRC(ssrc, cname, msid, cname);
		auto track = pc->addTrack(audio);
		// create RTP configuration
		auto rtpConfig = std::shared_ptr<rtc::RtpPacketizationConfig>(new rtc::RtpPacketizationConfig(ssrc, cname, payloadType, rtc::OpusRtpPacketizer::defaultClockRate));
		// create packetizer
		auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
		// create opus handler
		auto opusHandler = std::make_shared<rtc::OpusPacketizationHandler>(packetizer);
		// add RTCP SR handler
		auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
		opusHandler->addToChain(srReporter);
		// add RTCP NACK handler
		auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
		opusHandler->addToChain(nackResponder);
		// set handler
		track->setRtcpHandler(opusHandler);
		track->onOpen(onOpen);
		auto trackData = std::make_shared<Connection::Track>(track, srReporter);
		return trackData;
	}

	/// Add client to stream
	/// @param client Client
	/// @param adding_video True if adding video
	void addToStream(const std::shared_ptr<Connection>& client, bool isAddingVideo) {
		if (client->getState() == Connection::State::Waiting) {
			client->setState(isAddingVideo ? Connection::State::WaitingForAudio : Connection::State::WaitingForVideo);
		} else if ((client->getState() == Connection::State::WaitingForAudio && !isAddingVideo)
				   || (client->getState() == Connection::State::WaitingForVideo && isAddingVideo)) {

			// Audio and video tracks are collected now
			assert(client->video.has_value() && client->audio.has_value());

			auto video = client->video.value();
			auto audio = client->audio.value();

			auto currentTime_us = double(currentTimeInMicroSeconds());
			auto currentTime_s = currentTime_us / (1000 * 1000);

//			// set start time of stream
// TODO
//			video->sender->rtpConfig->setStartTime(currentTime_s, rtc::RtpPacketizationConfig::EpochStart::T1970);
//			audio->sender->rtpConfig->setStartTime(currentTime_s, rtc::RtpPacketizationConfig::EpochStart::T1970);

//			if (avStream.has_value()) {
//				sendInitialNalus(avStream.value(), video);
//			}

			client->setState(Connection::State::Ready);
		}
		if (client->getState() == Connection::State::Ready) {
            if (hasStream()) {
                startStream();
            }
		}
	}

	/// Start stream
	void startStream() {
		if (unlikely(!avStream.has_value())) {
			LOGE << "Stream is not set";
		}

		avStream.value()->start();
	}


private:
    /// websocket handler
    std::shared_ptr<rtc::WebSocket> ws = nullptr;

	/// all connected clients
	std::unordered_map<std::string, std::shared_ptr<Connection>> clients{};

	/// Audio and video stream
	std::optional<std::shared_ptr<Stream>> avStream = std::nullopt;

	/// Receiver callback
	std::optional<std::function<void(rtc::binary)>> receiver = std::nullopt;

	const std::string defaultIPAddress = "app.hela.me";
	const uint16_t defaultPort = 8000;
	std::string ip_address = defaultIPAddress;
	uint16_t port = defaultPort;

    bool hasStream() {
        return avStream.has_value();
    }

    bool hasReceiver() {
        return receiver.has_value();
    }
};
