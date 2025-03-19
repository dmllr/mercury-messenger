#include <chrono>
#include <csignal>
#include <memory>
#include <mutex>
#include <string>
#include <optional>
#include <thread>
#include <unordered_map>

#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>
#include <nlohmann/json.hpp>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "args_parser.hpp"
#include "firebase_verifier.hpp"

struct Log {
  static constexpr auto tag = "helaeillance";
  static constexpr auto logLevel = plog::debug;

private:
  // NOLINTNEXTLINE(*-statically-constructed-objects)
  inline static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
  inline const static plog::Logger<0>& logger = plog::init(logLevel, &appender);
};

class SignalingServer {
  using Json = nlohmann::json;
  using UserId = std::string;
  using PeerId = std::string;

  enum class PeerType : char {
    uninitialized,
    streamer,
    viewer
  };

  struct Peer {
    PeerId id;
    PeerType type{PeerType::uninitialized};
    std::string name;
    std::shared_ptr<rtc::WebSocket> ws;
    Json profile;
  };

public:
  explicit SignalingServer(const rtc::WebSocketServer::Configuration& config) : verifier_(std::make_unique<FirebaseVerifier>()) {
    server_ = std::make_unique<rtc::WebSocketServer>(config);
    server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
      HandleConnection(ws);
    });
  }

private:
  std::unique_ptr<rtc::WebSocketServer> server_;
  std::unique_ptr<FirebaseVerifier> verifier_;
  std::unordered_map<UserId, std::unordered_map<PeerId, Peer>> peers_;
  std::mutex peersMutex_;

  void HandleConnection(std::shared_ptr<rtc::WebSocket> ws) {
    LOGD << "New connection established";
    auto user_id = std::make_shared<std::string>();
    auto peer_id = std::make_shared<std::string>();

    ws->onMessage(nullptr, [ws, user_id, peer_id, this](const std::string& msg) {
      try {
        LOGD << "Received message: length=" << msg.length();
        auto json = Json::parse(msg);

        if (!ValidateMessage(ws, json, *user_id)) {
          LOGD << "Message validation failed";
          return;
        }

        const auto& type = json["type"].get<std::string>();
        LOGD << "Processing message type: " << type;
        if (type == "publish") {
          HandlePublish(ws, json, *user_id);
          // Update peer_id when a peer publishes
          if (json.contains("peer_id")) {
            *peer_id = json["peer_id"];
            LOGD << "Updated peer_id: " << *peer_id;
          }
        } else if (type == "get_streamers") {
          LOGD << "Processing get_streamers request for user_id: " << *user_id;
          SendStreamers(ws, *user_id);
        } else {
          LOGD << "Relaying message from user_id: " << *user_id << ", peer_id: " << *peer_id;
          RelayMessage(json, *user_id, *peer_id);
        }
      } catch (const std::exception& e) {
        LOGE << "Message handling error: " << e.what();
        SendError(ws, "invalid_message", e.what());
      }
    });

    ws->onOpen([]() {
      LOGD << "WebSocket connection opened";
    });

    ws->onClosed([user_id, peer_id, this] {
      LOGD << "WebSocket connection closed for user_id: " << *user_id << ", peer_id: " << *peer_id;
      bool is_notification_required = false;
      {
        std::lock_guard<std::mutex> lock(peersMutex_);
        if (!user_id->empty() && !peer_id->empty()) {
          auto ere = peers_[*user_id].erase(*peer_id);
          if (ere != 0U) {
            LOGD << "Removed peer: " << *peer_id << " for user: " << *user_id;
            is_notification_required = true;
          }
        }
      }
      if (is_notification_required) {
        LOGD << "Notifying peer disconnection: " << *peer_id;
        NotifyPeerDisconnected(*user_id, *peer_id);
        BroadcastStreamers(*user_id);
      }
    });
  }

  void SendStreamers(const std::shared_ptr<rtc::WebSocket>& ws, const UserId& user_id) {
    LOGD << "Building streamers list for user_id: " << user_id;
    Json response;
    response["type"] = "streamers";
    response["peers"] = Json::array();

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      // Collect all streamers for the user
      if (peers_.find(user_id) != peers_.end()) {
        for (const auto& [peerId, peer]: peers_[user_id]) {
          if (peer.type == PeerType::streamer) {
            LOGD << "Adding streamer: " << peerId << " to response";
            response["peers"].push_back({
                {"id", peerId},
                {"name", peer.name},
                {"profile", peer.profile}
            });
          }
        }
      }
    }

    // Send the response to the requesting rtc::WebSocket
    LOGD << "Sending streamers response with " << response["peers"].size() << " peers";
    ws->send(response.dump());
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void RelayMessage(Json& msg, const UserId& user_id, const PeerId& peer_id) {
    PeerId destination_id{};
    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      destination_id = msg.value("to", "");
      LOGD << "Relaying message from: " << peer_id << " to: " << destination_id;

      if (peers_.find(user_id) == peers_.end() || peers_[user_id].find(destination_id) == peers_[user_id].end()) {
        LOGE << "Peer " << destination_id << " not found for user " << user_id;
        LOGD << "Failed to relay message: destination peer not found";
        return;
      }
    }

    // Forward the message to the destination peer
    if (!destination_id.empty()) {
      auto& destination_peer = peers_[user_id][destination_id];
      msg["from"] = peer_id;
      LOGD << "Forwarding message to destination: " << destination_id;
      destination_peer.ws->send(msg.dump());
    }
  }

  bool ValidateMessage(std::shared_ptr<rtc::WebSocket> ws, const Json& msg, UserId& user_id) {
    LOGD << "Validating message token";
    auto token = msg.value("token", "");
    if (token.empty()) {
      LOGD << "Token validation failed: missing token";
      SendError(ws, "missing_token", "Token required");
      return false;
    }

    if (user_id.empty()) {
      auto uid = verifier_->VerifyToken(token);
      if (!uid) {
        LOGD << "Token verification failed for new user";
        SendError(ws, "invalid_token", "Token verification failed");
        return false;
      }
      user_id = *uid;
      LOGD << "User authenticated: " << user_id;
    } else {
      LOGD << "Verifying token for existing user: " << user_id;
      auto uid = verifier_->VerifyToken(token);
      if (!uid || *uid != user_id) {
        LOGD << "Token user mismatch: expected=" << user_id;
        SendError(ws, "user_mismatch", "Token user mismatch");
        return false;
      }
      LOGD << "User re-authenticated: " << user_id;
    }
    return true;
  }

  void HandlePublish(std::shared_ptr<rtc::WebSocket> ws, const Json& msg, const UserId& user_id) {
    PeerId peer_id = msg["peer_id"];
    LOGD << "Handling publish from user_id: " << user_id << ", peer_id: " << peer_id;
    Peer new_peer{
        .id = peer_id,
        .type = msg["kind"] == "streamer" ? PeerType::streamer : PeerType::viewer,
        .name = msg["name"],
        .ws = ws,
        .profile = msg.value("profile", Json::object())
    };
    LOGD << "Peer type: " << (new_peer.type == PeerType::streamer ? "streamer" : "viewer");

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      auto& user_peers = peers_[user_id];

      // Remove existing connection if present
      if (auto existing = user_peers.find(peer_id); existing != user_peers.end()) {
        if (existing->second.ws != ws) {
          LOGD << "Closing existing connection for peer_id: " << peer_id;
          existing->second.ws->close();
          user_peers.erase(existing);
        }
      }

      // Add new peer
      LOGD << "Adding new peer to user: " << user_id;
      user_peers[peer_id] = std::move(new_peer);
    }

    LOGD << "Broadcasting streamers after publish";
    BroadcastStreamers(user_id);
  }

  void BroadcastStreamers(const UserId& user_id) {
    LOGD << "Starting to broadcast streamers for user_id: " << user_id;
    std::vector<Peer> viewers;

    {
      std::lock_guard<std::mutex> lock(peersMutex_);

      for (const auto& [id, peer] : peers_[user_id]) {
        if (peer.type == PeerType::viewer) {
          viewers.push_back(peer);
        }
      }
    }

    for (const auto& peer : viewers) {
      LOGD << "Broadcasting streamers to the peer " << peer.id;
      SendStreamers(peer.ws, user_id);
    }
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void NotifyPeerDisconnected(const UserId& user_id, const PeerId& disconnected_peer_id) {
    LOGD << "Notifying peer disconnection: user_id=" << user_id << ", peer_id=" << disconnected_peer_id;
    Json notification = {
        {"type", "peer_disconnected"},
        {"id", disconnected_peer_id}
    };
    std::vector<std::shared_ptr<rtc::WebSocket>> targets;

    {
      std::lock_guard<std::mutex> lock(peersMutex_);

      // Determine the type of the disconnected peer
      auto user_it = peers_.find(user_id);
      if (user_it == peers_.end()) {
        LOGD << "User not found for notification: " << user_id;
        return;
      }

      auto& user_peers = user_it->second;
      auto peer_it = user_peers.find(disconnected_peer_id);
      if (peer_it == user_peers.end()) {
        LOGD << "Peer not found for notification: " << disconnected_peer_id;
        return;
      }

      auto target_type = (peer_it->second.type == PeerType::streamer) ? PeerType::viewer : PeerType::streamer;
      LOGD << "Target type for notifications: " << (target_type == PeerType::streamer ? "streamer" : "viewer");

      // Collect WebSockets of peers of the opposite type
      for (const auto& [id, peer] : user_peers) {
        if (peer.type == target_type) {
          LOGD << "Adding target to notification list: " << id;
          targets.push_back(peer.ws);
        }
      }
    }

    LOGD << "Sending disconnect notifications to " << targets.size() << " peers";
    for (const auto& ws : targets) {
      ws->send(notification.dump());
    }
  }

  static void SendError(std::shared_ptr<rtc::WebSocket> ws, const std::string& error, const std::string& message) {
    LOGD << "Sending error: " << error << " - " << message;
    Json response = {
        {"type", "error"},
        {"error", error},
        {"message", message}};
    ws->send(response.dump());
  }
};

// Global flag that can be accessed from the signal handler
std::atomic<bool> running(true); // NOLINT(*-avoid-non-const-global-variables)

// Signal handler function
void SignalHandler(int signum) {
  LOGI << "\nInterrupt signal (" << signum << ") received.\n";
  running = false;
}

int main(int argc, char* argv[]) {
  LOGD << "Starting signaling server";
  const auto default_port = uint16_t{8000};
  auto config = rtc::WebSocketServer::Configuration();
  config.port = default_port;

  LOGD << "Parsing command line arguments";
  ArgsParser args(argc, argv);
  const auto& port_arg = args.GetInt("--port");
  if (port_arg.has_value()) {
    LOGD << "Custom port requested: " << port_arg.value();
    config.port = port_arg.value();
  }

  LOGD << "Checking for SSL certificate files";
  config.certificatePemFile = args.Get("--ssl-cert");
  config.keyPemFile = args.Get("--ssl-cert-key");
  if (config.certificatePemFile.has_value()) {
    LOGD << "SSL certificate file: " << *config.certificatePemFile;
  }
  if (config.keyPemFile.has_value()) {
    LOGD << "SSL key file: " << *config.keyPemFile;
  }
  // temporary until mbedtls double lock issue is resolved
  //  if (config.certificatePemFile.has_value() && config.keyPemFile.has_value()) {
  //    config.enableTls = true;
  //  }

  // Register signal handler
  LOGD << "Registering signal handler";
  std::signal(SIGINT, SignalHandler);

  LOGD << "Creating SignalingServer instance";
  auto server = SignalingServer(config);

  LOGI << "Signaling server running on port " << config.port;
  if (config.enableTls) {
    LOGI << "  * connection is secure.";
  } else {
    LOGW << "  * connection is insecure!";
  }
  LOGI << "Press Ctrl+C to exit";

  // Keep the program running until SIGINT (Ctrl+C) is received
  LOGD << "Entering main loop";
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOGD << "Exiting application";
  LOGI << "Server stopped";
  return 0;
}
