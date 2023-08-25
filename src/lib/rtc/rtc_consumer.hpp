#pragma once

#include "logging/logging.h"

#include "rtc/rtc.h"
#include "nlohmann/json.hpp"
#include "peer.hpp"
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


class RtcConsumer {
	using json = nlohmann::json;

	std::thread server_thread;
public:
	explicit RtcConsumer() = default;

    void setReceiver(std::function<void(rtc::binary)> receiver) {
        this->receiver = receiver;
    }

    void startClient() {
        server_thread = std::thread {[this]() {
            start("client");
        }};
        server_thread.detach();
        LOGI << "server_thread ready";
    }


protected:

	void start(std::string_view localId) {
		try {
			rtc::Configuration config;
//			config.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
//			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80");
//			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80?transport=tcp");
//			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
//			config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443?transport=tcp");

			LOGI << "The local ID is: " << localId.data();

			ws = std::make_shared<rtc::WebSocket>();

			ws->onOpen([this]() {
				LOGI << "WebSocket connected, signaling ready";

                requestStream();
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
					wsOnMessage(make_weak_ptr(ws), message, config);
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

	void wsOnMessage(std::weak_ptr<rtc::WebSocket> wws, json message, const rtc::Configuration& config) {
		auto it = message.find("id");
		if (it == message.end())
			return;
		std::string id = it->get<std::string>();
		it = message.find("type");
		if (it == message.end())
			return;
		std::string type = it->get<std::string>();

		if (type == "offer") {
            auto sdp = message["sdp"].get<std::string>();
            auto offer = rtc::Description(sdp, type);
            std::shared_ptr<Peer> pc = createPeerConnection(config, id);
            pc->peerConnection->setRemoteDescription(offer);
            peers.emplace(id, pc);

            auto answer = pc->peerConnection->localDescription();
            json message = {
                    {"id", "server"},
                    {"debug", "consumer.wsOnMessage(offer)"},
                    {"type", answer->typeString()},
                    {"sdp", std::string(answer.value())}
            };
			ws->send(message.dump());

//				ws->send("{\n"
//						 "  \"id\": \"server\",\n"
//						 "  \"debug\": \"consumer.createPeerConnection\",\n"
//						 "  \"type\": \"" + description->typeString() + "\",\n"
//						 "  \"sdp\": \""+ std::string(description.value()) +"\"\n"
//						 "}");
        } else {
			throw std::runtime_error("Unknown message type " + type);
		}
	}

    void requestStream() {
		// TODO
        LOGD << "Sending a stream request message";
        json message = {
                {"id", "server"},
                {"debug", "consumer.requestStream()"},
                {"type", "streamRequest"},
                {"receiver", "client"}
        };
        ws->send(message.dump());
    }

    std::shared_ptr<Peer> createPeerConnection(const rtc::Configuration& config, const std::string& id) {
		auto pc = std::make_shared<rtc::PeerConnection>(config);

		pc->onStateChange([this, id](rtc::PeerConnection::State state) {
			LOGI << "State: " << (int)state;
			if (state == rtc::PeerConnection::State::Disconnected ||
				state == rtc::PeerConnection::State::Failed ||
				state == rtc::PeerConnection::State::Closed) {
				// remove disconnected peer
				LOGI << "peers.erase(" << id << "); ";
				MainThread.dispatch([this, id]() {
					LOGI << "main: peers.erase(" << id << "); ";
					peers.erase(id);
				});
			}
		});

		pc->onGatheringStateChange(
				[this, wpc = make_weak_ptr(pc), id](rtc::PeerConnection::GatheringState state) {
					LOGI << "Gathering State: " << (int)state;
					if (state == rtc::PeerConnection::GatheringState::Complete) {
                        // Gathering complete, send answer
						if(auto pc = wpc.lock()) {
							auto description = pc->localDescription();
							json message = {
									{"id", id},
									{"debug", "consumer.onGatheringStateChange(complete)"},
									{"type", description->typeString()},
									{"sdp", std::string(description.value())}
							};
                            ws->send(message.dump());
						}
					}
				});

		std::shared_ptr<Peer> peer(new Peer(pc));
        peer->video = addVideo(
            pc, 102, 10001, "video-stream", "cam-stream-1",
            [this, id, wc = make_weak_ptr(peer)]() {
                MainThread.dispatch([this, wc]() {
                    if (auto c = wc.lock()) {
                        addToStream(c, true);
                    }
                });
                LOGI << "Video from " << id << " opened";
            },
            [this, id, wc = make_weak_ptr(peer)](rtc::binary buffer) {
                LOGW << "VVVVVVVV";
            }
        );

        peer->audio = addAudio(
            pc, 111, 10002, "audio-stream", "cam-stream-1",
            [this, id, wc = make_weak_ptr(peer)]() {
                MainThread.dispatch([this, wc]() {
                    if (auto c = wc.lock()) {
                        addToStream(c, false);
                    }
                });
                LOGI << "Audio from " << id << " opened";
            },
            [this, id, wc = make_weak_ptr(peer)](rtc::binary buffer) {
                LOGW << "AAAAAAAA";
            }
        );

        peer->dataChannel = createDefaultDataChannel(pc, id);

		peer->setState(Peer::State::Ready);

//        if (auto ws = wws.lock()) {
//            auto description = pc->localDescription();
//            ws->send("{\n"
//                     "  \"id\": \"server\",\n"
//                     "  \"debug\": \"consumer.createPeerConnection\",\n"
//                     "  \"type\": \"" + description->typeString() + "\",\n"
//                     "  \"sdp\": \""+ std::string(description.value()) +"\"\n"
//                     "}");
//        }

		return peer;
    }

    static std::shared_ptr<Peer::Track> addVideo(
            const std::shared_ptr<rtc::PeerConnection>& pc,
            const uint8_t payloadType, const uint32_t ssrc, const std::string cname, const std::string msid,
            const std::function<void (void)> onOpen,
            const std::function<void (rtc::binary message)> onMessage) {
        auto video = rtc::Description::Video(cname, rtc::Description::Direction::RecvOnly);
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
        track->onMessage(onMessage, nullptr);

        return std::make_shared<Peer::Track>(track, srReporter);
    }

    static std::shared_ptr<Peer::Track> addAudio(
            const std::shared_ptr<rtc::PeerConnection>& pc,
            const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string msid,
            const std::function<void (void)> onOpen,
            const std::function<void (rtc::binary message)> onMessage) {
        auto audio = rtc::Description::Audio(cname, rtc::Description::Direction::RecvOnly);
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
        track->onMessage(onMessage, nullptr);

        return std::make_shared<Peer::Track>(track, srReporter);
    }

    void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) {
        if (peer->getState() == Peer::State::Waiting) {
            peer->setState(isAddingVideo ? Peer::State::WaitingForAudio : Peer::State::WaitingForVideo);
        } else if ((peer->getState() == Peer::State::WaitingForAudio && !isAddingVideo)
                   || (peer->getState() == Peer::State::WaitingForVideo && isAddingVideo)) {

            // Audio and video tracks are collected now
            assert(peer->video.has_value() && peer->audio.has_value());

            auto video = peer->video.value();
            auto audio = peer->audio.value();

            peer->setState(Peer::State::Ready);
        }
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


private:
    /// websocket handler
    std::shared_ptr<rtc::WebSocket> ws = nullptr;

	/// all connected peers
	std::unordered_map<std::string, std::shared_ptr<Peer>> peers{};

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
