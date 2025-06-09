#include <csignal>
#include <memory>
#include <mutex>
#include <string>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>
#include <nlohmann/json.hpp>
#include <utility>
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "args_parser.hpp"
#include "firebase_verifier.hpp"

constexpr auto logInstanceId = 454;
#define LGV PLOG_(logInstanceId, plog::verbose) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGD PLOG_(logInstanceId, plog::debug) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGI PLOG_(logInstanceId, plog::info) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGW PLOG_(logInstanceId, plog::warning) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGE PLOG_(logInstanceId, plog::error) // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)

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
  explicit SignalingServer(const rtc::WebSocketServer::Configuration& config, bool show_connections = false)
      : verifier_(std::make_unique<FirebaseVerifier>()), show_connections_(show_connections) {
    server_ = std::make_unique<rtc::WebSocketServer>(config);
    server_->onClient([this](const std::shared_ptr<rtc::WebSocket>& ws) {
      HandleConnection(ws);
    });
  }

private:
  std::unique_ptr<rtc::WebSocketServer> server_;
  std::unique_ptr<FirebaseVerifier> verifier_;
  std::unordered_map<UserId, std::unordered_map<PeerId, Peer>> peers_;
  std::mutex peersMutex_;
  bool show_connections_;

  void HandleConnection(const std::shared_ptr<rtc::WebSocket>& ws) {
    LGD << "New connection established";
    auto user_id = std::make_shared<std::string>();
    auto peer_id = std::make_shared<std::string>();

    ws->onMessage(nullptr, [this, ws, user_id, peer_id](const std::string& msg) {
      HandleMessage(ws, msg, *user_id, *peer_id);
    });

    ws->onOpen([]() {
      LGD << "WebSocket connection opened";
    });

    ws->onClosed([this, user_id, peer_id]() {
      HandleDisconnection(*user_id, *peer_id);
    });
  }

  void HandleMessage(const std::shared_ptr<rtc::WebSocket>& ws, const std::string& msg,
                     UserId& user_id, PeerId& peer_id) {
    try {
      LGD << "Received message: length=" << msg.length();

      LGV << "<< " << SanitizeMessageForLogging(msg);

      auto json = Json::parse(msg);

      if (!ValidateMessage(ws, json, user_id)) {
        LGD << "Message validation failed";
        return;
      }

      const auto& type = json["type"].get<std::string>();
      LGD << "Processing message type: " << type;

      if (type == "publish") {
        HandlePublish(ws, json, user_id);
        // Update peer_id when a peer publishes
        if (json.contains("peer_id")) {
          peer_id = json["peer_id"];
          LGD << "Updated peer_id: " << peer_id;
        }
      } else if (type == "get_streamers") {
        SendStreamers(ws, user_id);
      } else {
        RelayMessage(json, user_id, peer_id);
      }
    } catch (const std::exception& e) {
      SendError(ws, "invalid_message", e.what());
    }
  }

  void HandleDisconnection(const UserId& user_id, const PeerId& peer_id) {
    LGD << "WebSocket connection closed for user_id: " << user_id << ", peer_id: " << peer_id;
    bool is_notification_required = false;

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      if (!user_id.empty() && !peer_id.empty()) {
        auto ere = peers_[user_id].erase(peer_id);
        if (ere != 0U) {
          LGD << "Removed peer: " << peer_id << " for user: " << user_id;
          is_notification_required = true;
        }
      }
    }

    if (is_notification_required) {
      LGD << "Notifying peer disconnection: " << peer_id;
      NotifyPeerDisconnected(user_id, peer_id);
      BroadcastStreamers(user_id);
    }

    // Show connections status if enabled
    if (show_connections_) {
      ShowConnectionsStatus();
    }
  }

  void SendStreamers(const std::shared_ptr<rtc::WebSocket>& ws, const UserId& user_id) {
    LGD << "Processing get_streamers request for user_id: " << user_id;
    Json response;
    response["type"] = "streamers";
    response["peers"] = Json::array();

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      // Collect all streamers for the user
      if (peers_.find(user_id) != peers_.end()) {
        for (const auto& [peerId, peer]: peers_[user_id]) {
          if (peer.type == PeerType::streamer) {
            LGD << "Adding streamer: " << peerId << " to response";
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
    LGD << "Sending streamers response with " << response["peers"].size() << " peers";
    LGV << ">> " << SanitizeMessageForLogging(response.dump());
    ws->send(response.dump());
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void RelayMessage(Json& msg, const UserId& user_id, const PeerId& peer_id) {
    PeerId destination_id{};
    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      destination_id = msg.value("to", "");
      LGD << "Relaying message from: " << peer_id << " to: " << destination_id;

      if (peers_.find(user_id) == peers_.end() || peers_[user_id].find(destination_id) == peers_[user_id].end()) {
        LGE << "Peer " << destination_id << " not found for user " << user_id;
        LGD << "Failed to relay message: destination peer not found";
        return;
      }
    }

    // Forward the message to the destination peer
    if (!destination_id.empty()) {
      auto& destination_peer = peers_[user_id][destination_id];
      msg["from"] = peer_id;
      LGD << "Forwarding message to destination: " << destination_id;

      std::string message_str = msg.dump();
      LGV << ">> " << SanitizeMessageForLogging(message_str);
      destination_peer.ws->send(message_str);
    }
  }

  bool ValidateMessage(const std::shared_ptr<rtc::WebSocket>& ws, const Json& msg, UserId& user_id) {
    auto token = msg.value("token", "");
    if (token.empty()) {
      SendError(ws, "missing_token", "Token required");
      return false;
    }

    if (user_id.empty()) {
      auto uid = verifier_->VerifyToken(token);
      if (!uid) {
        SendError(ws, "invalid_token", "Token verification failed");
        return false;
      }
      user_id = *uid;
      LGD << "User authenticated: " << user_id;
    } else {
      auto uid = verifier_->VerifyToken(token);
      if (!uid || *uid != user_id) {
        SendError(ws, "user_mismatch", "Token user mismatch");
        return false;
      }
      LGD << "User re-authenticated: " << user_id;
    }
    return true;
  }

  void HandlePublish(const std::shared_ptr<rtc::WebSocket>& ws, const Json& msg, const UserId& user_id) {
    PeerId peer_id = msg["peer_id"];
    LGD << "Handling publish from user_id: " << user_id << ", peer_id: " << peer_id;
    Peer new_peer{
        .id = peer_id,
        .type = msg["kind"] == "streamer" ? PeerType::streamer : PeerType::viewer,
        .name = msg["name"],
        .ws = ws,
        .profile = msg.value("profile", Json::object())
    };
    LGD << "Peer type: " << (new_peer.type == PeerType::streamer ? "streamer" : "viewer");

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      auto& user_peers = peers_[user_id];

      // Remove existing connection if present
      if (auto existing = user_peers.find(peer_id); existing != user_peers.end()) {
        if (existing->second.ws != ws) {
          LGD << "Closing existing connection for peer_id: " << peer_id;
          existing->second.ws->close();
          user_peers.erase(existing);
        }
      }

      LGD << "Adding new peer to user: " << user_id;
      user_peers[peer_id] = std::move(new_peer);
    }

    LGD << "Broadcasting streamers after publish";
    BroadcastStreamers(user_id);

    // Show connections status if enabled
    if (show_connections_) {
      ShowConnectionsStatus();
    }
  }

  void BroadcastStreamers(const UserId& user_id) {
    LGD << "Starting to broadcast streamers for user_id: " << user_id;
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
      LGD << "Broadcasting streamers to the peer " << peer.id;
      SendStreamers(peer.ws, user_id);
    }
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void NotifyPeerDisconnected(const UserId& user_id, const PeerId& disconnected_peer_id) {
    LGD << "Notifying peer disconnection: user_id=" << user_id << ", peer_id=" << disconnected_peer_id;
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
        LGD << "User not found for notification: " << user_id;
        return;
      }

      auto& user_peers = user_it->second;
      auto peer_it = user_peers.find(disconnected_peer_id);
      if (peer_it == user_peers.end()) {
        LGD << "Peer not found for notification: " << disconnected_peer_id;
        return;
      }

      auto target_type = (peer_it->second.type == PeerType::streamer) ? PeerType::viewer : PeerType::streamer;

      // Collect WebSockets of peers of the opposite type
      for (const auto& [id, peer] : user_peers) {
        if (peer.type == target_type) {
          targets.push_back(peer.ws);
        }
      }
    }

    LGD << "Sending disconnect notifications to " << targets.size() << " peers";
    std::string notification_str = notification.dump();
    LGV << ">> " << notification_str;

    for (const auto& ws : targets) {
      ws->send(notification_str);
    }
  }

  static void SendError(const std::shared_ptr<rtc::WebSocket>& ws, const std::string& error, const std::string& message) {
    LGD << "Sending error response: " << error << " - " << message;
    Json response = {
        {"type", "error"},
        {"error", error},
        {"message", message}};

    std::string response_str = response.dump();
    LGV << ">> " << response_str;
    ws->send(response_str);
  }

  // Helper method to sanitize messages for logging
  static auto SanitizeMessageForLogging(const std::string& message) -> std::string {
    try {
      auto json = Json::parse(message);

      // Remove token if present
      if (json.contains("token")) {
        json["token"] = "[REDACTED]";
      }

      if (json.contains("profile") && (json["profile"].is_object() || json["profile"].is_array())) {
        auto element_count = 0ULL;
        if (json["profile"].is_array()) {
          element_count = json["profile"].size();
        }
        json["profile"] = "[" + std::to_string(element_count) + " elements]";
      }

      return json.dump();
    } catch (const std::exception&) {
      // If we can't parse as JSON, return as is
      return message;
    }
  }

  void ShowConnectionsStatus() {
    std::lock_guard<std::mutex> lock(peersMutex_);

    if (peers_.empty()) {
      std::cout << "No active connections" << std::endl;
      return;
    }

    // Display connections organized by user
    for (const auto& [userId, userPeers] : peers_) {
      std::cout << userId << std::endl;

      if (userPeers.empty()) {
        std::cout << "  |-- (no peers)" << std::endl;
      } else {
        for (const auto& [peerId, peer] : userPeers) {
          std::string typeStr = (peer.type == PeerType::streamer) ? "streamer" : "viewer";
          std::cout << "  |-- [" << typeStr << "] " << peer.name << std::endl;
        }
      }
    }

    std::cout << std::endl;
  }
};

namespace {

std::condition_variable exitCondition;  // NOLINT(*-avoid-non-const-global-variables, *-statically-constructed-objects)
std::mutex exitMutex;                   // NOLINT(*-avoid-non-const-global-variables, *-statically-constructed-objects)
bool shouldExit = false;                // NOLINT(*-avoid-non-const-global-variables)

} // namespace

// Signal handler function
void SignalHandler(int signum) {
  LGI << "\nInterrupt signal (" << signum << ") received.\n";
  {
    std::lock_guard<std::mutex> lock(exitMutex);
    shouldExit = true;
  }
  exitCondition.notify_all();
}

plog::Severity ParseLogLevel(const std::string& level) {
  if (level == "none") { return plog::none; }
  if (level == "fatal") { return plog::fatal; }
  if (level == "error") { return plog::error; }
  if (level == "warning") { return plog::warning; }
  if (level == "info") { return plog::info; }
  if (level == "debug") { return plog::debug; }
  if (level == "verbose") { return plog::verbose; }
  return plog::info; // Default to info
}

int main(int argc, char* argv[]) { // NOLINT(*-function-cognitive-complexity), false-positive due to logging
  ArgsParser args(argc, argv);

  // Check if show-connections mode is enabled
  bool show_connections = args.Has("--show-connections");

  // Initialize logging
  static plog::ColorConsoleAppender<plog::TxtFormatter> appender;

  // set default instance (used in deps) to only show warnings and higher
  plog::init(plog::warning, &appender);

  // Default log level is info, but can be overridden by command line
  auto log_level_str = args.Get("--log-level").value_or("info");
  if (show_connections) {
    log_level_str = "none";
  }
  auto log_level = ParseLogLevel(log_level_str);
  plog::init<logInstanceId>(log_level, &appender);

  LGD << "Starting signaling server";
  const auto default_port = uint16_t{8000};
  auto config = rtc::WebSocketServer::Configuration();
  config.port = default_port;

  const auto& port_arg = args.GetInt("--port");
  if (port_arg.has_value()) {
    config.port = port_arg.value();
  }

  config.certificatePemFile = args.Get("--ssl-cert");
  config.keyPemFile = args.Get("--ssl-cert-key");
  if (config.certificatePemFile.has_value()) {
    LGD << "SSL certificate file: " << *config.certificatePemFile;
  }
  if (config.keyPemFile.has_value()) {
    LGD << "SSL key file: " << *config.keyPemFile;
  }
  if (config.certificatePemFile.has_value() && config.keyPemFile.has_value()) {
    config.enableTls = true;
  }

  // Register signal handler
  std::signal(SIGINT, SignalHandler);

  auto server = SignalingServer(config, show_connections);

  if (!show_connections) {
    LGI << "Signaling server running on port " << config.port;
    LGI << "Log level set to: " << log_level_str;
    if (config.enableTls) {
      LGI << "  * connection is secure.";
    } else {
      LGW << "  * connection is insecure!";
    }
    LGI << "Press Ctrl+C to exit";
  } else {
    // In show-connections mode, display initial status
    std::cout << "Connection Monitor Mode - Press Ctrl+C to exit" << std::endl;
    std::cout << "Server running on port " << config.port << std::endl;
  }

  // Wait for program termination
  LGD << "Entering wait state";
  {
    std::unique_lock<std::mutex> lock(exitMutex);
    exitCondition.wait(lock, [] { return shouldExit; });
  }
  LGD << "Exit condition triggered";

  LGD << "Exiting application";
  if (!show_connections) {
    LGI << "Server stopped";
  } else {
    std::cout << "\nServer stopped" << std::endl;
  }

  return 0;
}
