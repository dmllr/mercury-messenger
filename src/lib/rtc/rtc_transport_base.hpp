#pragma once

#include "logging/logging.h"
#include "utils/memory.h"

#include "rtc/rtc.h"
#include "nlohmann/json.hpp"
#include "peer.hpp"
#include "dispatch_queue.hpp"

#include "stream.hpp"

#include <memory>
#include <thread>
#include <unordered_map>

extern DispatchQueue MainThread;

class RtcTransportBase {
	using json = nlohmann::json;

    const std::string ip_address = "app.hela.me";
    const uint16_t port = 8000;

public:
	explicit RtcTransportBase() {
        ws_config.caCertificatePemFile = ZEROSSL_CA_BUNDLE;
    }

    auto hasPeer() -> bool {
        return !peers.empty();
    }

protected:
    virtual void wsOnMessage(json message) = 0;
    virtual void onPeerReady(const std::shared_ptr<Peer>& peer) = 0;

    class RtcConfiguration {
    public:
        static auto config() -> rtc::Configuration& {
            static rtc::Configuration config = createRtcConfiguration();
            return config;
        }

    private:
        static auto createRtcConfiguration() -> rtc::Configuration {
            rtc::Configuration config;
            config.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80");
            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80?transport=tcp");
            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
            config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443?transport=tcp");
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
				if (!std::holds_alternative<std::string>(data)) {
					return;
                }

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
				if (ws.isClosed()) {
					return;
                }
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

    auto createDefaultPeerConnection(const std::string& id) -> std::shared_ptr<rtc::PeerConnection> {
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

    static auto createDefaultDataChannel(std::shared_ptr<rtc::PeerConnection> pc, const std::string& id) -> std::shared_ptr<rtc::DataChannel> {
        auto dc = pc->createDataChannel("ping-pong");
        dc->onOpen([id, wdc = make_weak_ptr(dc)]() {
            if (auto dc = wdc.lock()) {
                dc->send("Ping");
            }
        });

        dc->onMessage(nullptr, [id, wdc = make_weak_ptr(dc)](const std::string& msg) {
            LOGD << "Message from " << id << " received: " << msg;
            if (auto dc = wdc.lock()) {
                dc->send("Ping");
            }
        });

        return dc;
    }
private:
    static inline std::string ZEROSSL_CA_BUNDLE =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIG1TCCBL2gAwIBAgIQbFWr29AHksedBwzYEZ7WvzANBgkqhkiG9w0BAQwFADCB\n"
      "iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n"
      "cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n"
      "BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjAw\n"
      "MTMwMDAwMDAwWhcNMzAwMTI5MjM1OTU5WjBLMQswCQYDVQQGEwJBVDEQMA4GA1UE\n"
      "ChMHWmVyb1NTTDEqMCgGA1UEAxMhWmVyb1NTTCBSU0EgRG9tYWluIFNlY3VyZSBT\n"
      "aXRlIENBMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAhmlzfqO1Mdgj\n"
      "4W3dpBPTVBX1AuvcAyG1fl0dUnw/MeueCWzRWTheZ35LVo91kLI3DDVaZKW+TBAs\n"
      "JBjEbYmMwcWSTWYCg5334SF0+ctDAsFxsX+rTDh9kSrG/4mp6OShubLaEIUJiZo4\n"
      "t873TuSd0Wj5DWt3DtpAG8T35l/v+xrN8ub8PSSoX5Vkgw+jWf4KQtNvUFLDq8mF\n"
      "WhUnPL6jHAADXpvs4lTNYwOtx9yQtbpxwSt7QJY1+ICrmRJB6BuKRt/jfDJF9Jsc\n"
      "RQVlHIxQdKAJl7oaVnXgDkqtk2qddd3kCDXd74gv813G91z7CjsGyJ93oJIlNS3U\n"
      "gFbD6V54JMgZ3rSmotYbz98oZxX7MKbtCm1aJ/q+hTv2YK1yMxrnfcieKmOYBbFD\n"
      "hnW5O6RMA703dBK92j6XRN2EttLkQuujZgy+jXRKtaWMIlkNkWJmOiHmErQngHvt\n"
      "iNkIcjJumq1ddFX4iaTI40a6zgvIBtxFeDs2RfcaH73er7ctNUUqgQT5rFgJhMmF\n"
      "x76rQgB5OZUkodb5k2ex7P+Gu4J86bS15094UuYcV09hVeknmTh5Ex9CBKipLS2W\n"
      "2wKBakf+aVYnNCU6S0nASqt2xrZpGC1v7v6DhuepyyJtn3qSV2PoBiU5Sql+aARp\n"
      "wUibQMGm44gjyNDqDlVp+ShLQlUH9x8CAwEAAaOCAXUwggFxMB8GA1UdIwQYMBaA\n"
      "FFN5v1qqK0rPVIDh2JvAnfKyA2bLMB0GA1UdDgQWBBTI2XhootkZaNU9ct5fCj7c\n"
      "tYaGpjAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHSUE\n"
      "FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwIgYDVR0gBBswGTANBgsrBgEEAbIxAQIC\n"
      "TjAIBgZngQwBAgEwUAYDVR0fBEkwRzBFoEOgQYY/aHR0cDovL2NybC51c2VydHJ1\n"
      "c3QuY29tL1VTRVJUcnVzdFJTQUNlcnRpZmljYXRpb25BdXRob3JpdHkuY3JsMHYG\n"
      "CCsGAQUFBwEBBGowaDA/BggrBgEFBQcwAoYzaHR0cDovL2NydC51c2VydHJ1c3Qu\n"
      "Y29tL1VTRVJUcnVzdFJTQUFkZFRydXN0Q0EuY3J0MCUGCCsGAQUFBzABhhlodHRw\n"
      "Oi8vb2NzcC51c2VydHJ1c3QuY29tMA0GCSqGSIb3DQEBDAUAA4ICAQAVDwoIzQDV\n"
      "ercT0eYqZjBNJ8VNWwVFlQOtZERqn5iWnEVaLZZdzxlbvz2Fx0ExUNuUEgYkIVM4\n"
      "YocKkCQ7hO5noicoq/DrEYH5IuNcuW1I8JJZ9DLuB1fYvIHlZ2JG46iNbVKA3ygA\n"
      "Ez86RvDQlt2C494qqPVItRjrz9YlJEGT0DrttyApq0YLFDzf+Z1pkMhh7c+7fXeJ\n"
      "qmIhfJpduKc8HEQkYQQShen426S3H0JrIAbKcBCiyYFuOhfyvuwVCFDfFvrjADjd\n"
      "4jX1uQXd161IyFRbm89s2Oj5oU1wDYz5sx+hoCuh6lSs+/uPuWomIq3y1GDFNafW\n"
      "+LsHBU16lQo5Q2yh25laQsKRgyPmMpHJ98edm6y2sHUabASmRHxvGiuwwE25aDU0\n"
      "2SAeepyImJ2CzB80YG7WxlynHqNhpE7xfC7PzQlLgmfEHdU+tHFeQazRQnrFkW2W\n"
      "kqRGIq7cKRnyypvjPMkjeiV9lRdAM9fSJvsB3svUuu1coIG1xxI1yegoGM4r5QP4\n"
      "RGIVvYaiI76C0djoSbQ/dkIUUXQuB8AL5jyH34g3BZaaXyvpmnV4ilppMXVAnAYG\n"
      "ON51WhJ6W0xNdNJwzYASZYH+tmCWI+N60Gv2NNMGHwMZ7e9bXgzUCZH5FaBFDGR5\n"
      "S9VWqHB73Q+OyIVvIbKYcSc2w/aSuFKGSA==\n"
      "-----END CERTIFICATE-----";

protected:
    rtc::WebSocketConfiguration ws_config;
    rtc::WebSocket ws{ws_config};

	/// all connected peers
	std::unordered_map<std::string, std::shared_ptr<Peer>> peers;
};
