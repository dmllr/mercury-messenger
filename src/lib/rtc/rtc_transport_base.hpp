#pragma once

#include "logging/logging.h"
#include "memory.h"

#include "rtc/rtc.h"
#include "nlohmann/json.hpp"
#include "peer.hpp"
#include "dispatch_queue.hpp"

#include "stream.hpp"

#include <memory>
#include <thread>
#include <unordered_map>


DispatchQueue MainThread("Main", 1);


class RtcTransportBase {
	using json = nlohmann::json;

    const std::string ip_address = "app.hela.me";
    const uint16_t port = 8000;

public:
	explicit RtcTransportBase() = default;

    bool hasPeer() {
        return !peers.empty();
    }

protected:
    virtual void wsOnMessage(json message) = 0;
    virtual void onPeerReady(const std::shared_ptr<Peer>& peer) = 0;

    class RtcConfiguration {
    public:
        static rtc::Configuration& config() {
            static rtc::Configuration config = createRtcConfiguration();
            return config;
        }

    private:
        static rtc::Configuration createRtcConfiguration() {
            rtc::Configuration config;
//            config.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
//            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80");
//            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80?transport=tcp");
//            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
//            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443?transport=tcp");
            return config;
        }
    };

	void start(std::string_view localId) {
		try {
			LOGI << "The local ID is: " << localId.data();

			ws.onOpen([this]() {
				LOGI << "WebSocket connected, signaling ready";
			});

			ws.onClosed([]() {
				LOGI << "WebSocket closed";
			});

			ws.onError([](const std::string& error) {
				LOGI << "WebSocket failed: " << error.c_str();
			});

			ws.onMessage([&](std::variant<rtc::binary, std::string> data) {
				if (!std::holds_alternative<std::string>(data))
					return;

				json message = json::parse(std::get<std::string>(data));
				MainThread.dispatch([this, message]() {
					wsOnMessage(message);
				});
			});

			const std::string url = "wss://" + ip_address + ":" + std::to_string(port) + "/" + localId.data();
			LOGI << "Url is " << url.c_str();
			ws.open(url);

			LOGI << "Waiting for signaling to be connected...";
			while (!ws.isOpen()) {
				if (ws.isClosed())
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

//			while (true) {
//				std::this_thread::sleep_for(std::chrono::milliseconds(100));
//				// TODO Stop option
//			}
//
//			LOGI << "Cleaning up...";
//			return;
		} catch (const std::exception& e) {
			LOGI << "Error: " << e.what();
			return;
		}
	}

    std::shared_ptr<rtc::PeerConnection> createDefaultPeerConnection(const std::string& id) {
		auto pc = std::make_shared<rtc::PeerConnection>(RtcConfiguration::config());

		pc->onStateChange([this, id](rtc::PeerConnection::State state) {
			LOGI << "State: " << (int)state;
			if (state == rtc::PeerConnection::State::Disconnected ||
				state == rtc::PeerConnection::State::Failed ||
				state == rtc::PeerConnection::State::Closed) {
				// remove disconnected peer
				MainThread.dispatch([this, id]() {
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
									{"debug", "onGatheringStateChange(complete)"},
									{"type", description->typeString()},
									{"sdp", std::string(description.value())}
							};
                            ws.send(message.dump());
						}
					}
				});

        return pc;
    }

    void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) {
        if (peer->getState() == Peer::State::Waiting) {
            peer->setState(isAddingVideo ? Peer::State::WaitingForAudio : Peer::State::WaitingForVideo);
        } else if ((peer->getState() == Peer::State::WaitingForAudio && !isAddingVideo)
                   || (peer->getState() == Peer::State::WaitingForVideo && isAddingVideo)) {
            // Audio and video tracks are collected now
            assert(peer->video.has_value() && peer->audio.has_value());

//            auto video = peer->video.value();
//            auto audio = peer->audio.value();
//
//            auto currentTime_us = double(currentTimeInMicroSeconds());
//            auto currentTime_s = currentTime_us / (1000 * 1000);
//
////			// set start time of stream
//// TODO
////			video->sender->rtpConfig->setStartTime(currentTime_s, rtc::RtpPacketizationConfig::EpochStart::T1970);
////			audio->sender->rtpConfig->setStartTime(currentTime_s, rtc::RtpPacketizationConfig::EpochStart::T1970);

            peer->setState(Peer::State::Ready);
        }

        if (peer->getState() == Peer::State::Ready) {
            onPeerReady(peer);
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
            LOGE << "Message from " << id << " received: " << msg;
            if (auto dc = wdc.lock()) {
                dc->send("Ping");
            }
        });

        return dc;
    }

protected:
    /// websocket handler
    rtc::WebSocketConfiguration ws_config {
        .caCertificatePemFile = "/Users/denis.miller/dev/hela-surveillance/data/ZeroSSL-CA/ca_bundle.pem",
    };
    rtc::WebSocket ws{ws_config};

	/// all connected peers
	std::unordered_map<std::string, std::shared_ptr<Peer>> peers{};
};
