#include <csignal>
#include <memory>
#include <mutex>
#include <string>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <thread>

#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>
#include <nlohmann/json.hpp>
#include <utility>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "args_parser.hpp"

constexpr auto logInstanceId = 454;
#define LGV PLOG_(logInstanceId, plog::verbose) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGD PLOG_(logInstanceId, plog::debug) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGI PLOG_(logInstanceId, plog::info) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGW PLOG_(logInstanceId, plog::warning) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGE PLOG_(logInstanceId, plog::error) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)

class SignalingServer {
  using Json = nlohmann::json;
  using PeerId = std::string;

  struct Peer {
    std::shared_ptr<rtc::WebSocket> ws;
  };

public:
  explicit SignalingServer(const rtc::WebSocketServer::Configuration& config) {
    server_ = std::make_unique<rtc::WebSocketServer>(config);
    server_->onClient([this](const std::shared_ptr<rtc::WebSocket>& wss) {
      HandleConnection(wss);
    });
  }

private:
  std::unique_ptr<rtc::WebSocketServer> server_;
  std::unordered_map<PeerId, Peer> peers_;
  std::mutex peersMutex_;

  void HandleConnection(const std::shared_ptr<rtc::WebSocket>& wss) {
    LGD << "New connection established";
    auto peer_id = std::make_shared<PeerId>();

    wss->onMessage(nullptr, [this, wss, peer_id](const std::string& msg) {
      HandleMessage(wss, msg, *peer_id);
    });

    wss->onClosed([this, peer_id]() {
      HandleDisconnection(*peer_id);
    });
  }

  void HandleMessage(const std::shared_ptr<rtc::WebSocket>& wss, const std::string& msg, PeerId& peer_id) {
    try {
      LGV << "<< " << msg;
      auto json = Json::parse(msg);
      auto type = json.value("type", "");

      if (peer_id.empty()) {
        // First message from a client must be for identification (login).
        if (type == "login" && json.contains("id")) {
          peer_id = json["id"].get<PeerId>();
          if (peer_id.empty()) {
            SendError(wss, "invalid_id", "Peer ID cannot be empty.");
            peer_id.clear(); // Ensure it remains empty on failure
            return;
          }

          const std::lock_guard<std::mutex> lock(peersMutex_);

          if (auto itr = peers_.find(peer_id); itr != peers_.end()) {
            LGI << "Peer " << peer_id << " is reconnecting. Closing old connection.";
            itr->second.ws->close();
          }

          peers_[peer_id] = {wss};
          LGI << "Peer registered: " << peer_id;

          // Broadcast the online status to all other peers
          Json const online_notification = {
              {"type", "peer_online"},
              {"id", peer_id}
          };
          for (const auto& [id, peer] : peers_) {
              if (id != peer_id && peer.ws && peer.ws->isOpen()) {
                  peer.ws->send(online_notification.dump());
              }
          }

        } else {
          SendError(wss, "login_required", "First message must be of type 'login' with an 'id'.");
          wss->close();
        }
      } else {
        // After identification, all other messages are treated as relay messages.
        if (!json.contains("to")) {
          SendError(wss, "missing_recipient", "Message must have a 'to' field for relaying.");
          return;
        }
        auto destination_id = json.value("to", "");
        if (destination_id.empty()) {
          SendError(wss, "empty_recipient", "Recipient 'to' field cannot be empty.");
          return;
        }

        LGD << "Relaying message from: " << peer_id << " to: " << destination_id;

        std::shared_ptr<rtc::WebSocket> dest_ws;
        {
          const std::lock_guard<std::mutex> lock(peersMutex_);
          auto itr = peers_.find(destination_id);
          if (itr == peers_.end()) {
            LGE << "Peer " << destination_id << " not found.";
            SendError(wss, "peer_not_found", "Peer '" + destination_id + "' not found.", destination_id);
            return;
          }
          dest_ws = itr->second.ws;
        }

        json["from"] = peer_id;
        std::string message_str = json.dump();
        LGV << ">> to " << destination_id << ": " << message_str;

        if (dest_ws) {
          dest_ws->send(message_str);
        }
      }
    } catch (const std::exception& e) {
      SendError(wss, "invalid_message", e.what());
    }
  }

  void HandleDisconnection(const PeerId& peer_id) {
    if (peer_id.empty()) {
      LGD << "Unidentified peer disconnected.";
      return;
    }

    const std::lock_guard<std::mutex> lock(peersMutex_);
    if (peers_.erase(peer_id) > 0) {
      LGI << "Peer disconnected and removed: " << peer_id;
    }
  }

  static void SendError(const std::shared_ptr<rtc::WebSocket>& wss, const std::string& error, const std::string& message, const std::optional<PeerId>& for_peer = std::nullopt) {
      if (!wss || !wss->isOpen()) {
          return;
      }
      LGE << "Sending error: " << error << " - " << message;
      Json response = {
          {"type", "error"},
          {"error", error},
          {"message", message}};

      if (for_peer) {
          response["for_peer"] = *for_peer;
      }
      wss->send(response.dump());
  }
};

namespace {

volatile std::sig_atomic_t signalReceived = 0;  // NOLINT(*-avoid-non-const-global-variables)

void SignalHandler(int signum) {
  LGI << "\nInterrupt signal (" << signum << ") received.\n";
  signalReceived = signum;
}

plog::Severity ParseLogLevel(const std::string& level) {
  if (level == "none") { return plog::none; }
  if (level == "fatal") { return plog::fatal; }
  if (level == "error") { return plog::error; }
  if (level == "warning") { return plog::warning; }
  if (level == "info") { return plog::info; }
  if (level == "debug") { return plog::debug; }
  if (level == "verbose") { return plog::verbose; }
  return plog::info;
}

} // namespace

int main(int argc, char* argv[]) {
  const ArgsParser args(argc, argv);

  static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
  plog::init(plog::warning, &appender);
  auto log_level_str = args.Get("--log-level").value_or("info");
  auto log_level = ParseLogLevel(log_level_str);
  plog::init<logInstanceId>(log_level, &appender);

  LGD << "Starting signaling server";
  constexpr auto default_port = uint16_t{8000};
  auto config = rtc::WebSocketServer::Configuration();
  config.port = default_port;

  const auto& port_arg = args.GetInt("--port");
  if (port_arg.has_value()) {
    config.port = static_cast<uint16_t>(port_arg.value());
  }

  config.certificatePemFile = args.Get("--ssl-cert");
  config.keyPemFile = args.Get("--ssl-cert-key");
  if (config.certificatePemFile.has_value() && config.keyPemFile.has_value()) {
    config.enableTls = true;
  }

  std::signal(SIGINT, SignalHandler);

  auto server = SignalingServer(config);

  LGI << "Signaling server running on port " << config.port;
  LGI << "Log level set to: " << log_level_str;
  if (config.enableTls) {
    LGI << "  * connection is secure.";
  } else {
    LGW << "  * connection is insecure!";
  }
  LGI << "Press Ctrl+C to exit";

  while (signalReceived == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LGD << "Exiting application";
  LGI << "Server stopped";

  return 0;
}
