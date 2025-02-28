#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <optional>
#include <thread>
#include <unordered_map>

#include "rtc/websocket.hpp"
#include "rtc/websocketserver.hpp"
#include <cpr/cpr.h>
#include <csignal>
#include <nlohmann/json.hpp>

#include "hela/eillance/logging/logging.h"

using Json = nlohmann::json;

// Configuration constants
const std::string serviceAccountPath = "C:\\Users\\DenisMiller\\Downloads\\eillancex-firebase-adminsdk-fbsvc-b42c47be20.json";
//const std::string serviceAccountPath = "/etc/secret/firebase-service-account.json";
const std::chrono::minutes tokenRefreshInterval{5};

class FirebaseVerifier {
public:
  FirebaseVerifier() {
    LoadServiceAccount();
  }

  std::optional<std::string> VerifyToken(const std::string& token) {
    try {
      // Use Google's token verification endpoint
      const std::string verify_url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup";

      // Make the verification request
      auto post = cpr::Post(
          cpr::Url{verify_url},
          cpr::Parameters{{"key", apiKey_}},  // API key from your service account
          cpr::Body{R"({"idToken":")" + token + R"("})"},
          cpr::Header{{"Content-Type", "application/json"}}
      );

      if (post.status_code != cpr::status::HTTP_OK) {
        LOGE << "Token verification failed: " << post.text;
        return std::nullopt;
      }

      // Parse the response
      Json response = Json::parse(post.text);

      // Check if the token is valid
      if (response["users"].empty()) {
        LOGE << "Token doesn't belong to any user";
        return std::nullopt;
      }

      // Return the user ID
      return response["users"][0]["localId"];

    } catch (const std::exception& e) {
      LOGE << "Token verification failed: " << e.what();
      return std::nullopt;
    }
  }

private:
  std::string projectId_;
  std::string apiKey_;

  void LoadServiceAccount() {
    std::ifstream file(serviceAccountPath);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open service account file: " + serviceAccountPath);
    }

    Json service_account;
    file >> service_account;
    projectId_ = service_account["project_id"];

    // Try to get API key from environment variable first
    const char* env_api_key = std::getenv("FIREBASE_API_KEY"); // NOLINT(*-mt-unsafe)
    if (env_api_key != nullptr) {
      apiKey_ = env_api_key;
    } else {
      // Fallback to a separate config file
      std::ifstream api_key_file("firebase_api_key.txt");
      if (api_key_file.is_open()) {
        std::getline(api_key_file, apiKey_);
      }
    }

    if (apiKey_.empty()) {
      LOGE << "Warning: API key not found. Please set FIREBASE_API_KEY environment variable or create firebase_api_key.txt";
    }
  }
};

class SignalingServer {
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
  SignalingServer(uint16_t port, bool ssl) : verifier_(std::make_unique<FirebaseVerifier>()) {
    rtc::WebSocketServer::Configuration config;
    config.port = port;
    config.enableTls = ssl;
    // TODO: Configure SSL certs

    server_ = std::make_unique<rtc::WebSocketServer>(config);
    server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) { HandleConnection(ws); });
  }

private:
  std::unique_ptr<rtc::WebSocketServer> server_;
  std::unique_ptr<FirebaseVerifier> verifier_;
  std::unordered_map<UserId, std::unordered_map<PeerId, Peer>> peers_;
  std::mutex peersMutex_;

  void HandleConnection(std::shared_ptr<rtc::WebSocket> ws) {
    auto user_id = std::make_shared<std::string>();
    auto peer_id = std::make_shared<std::string>();

    ws->onMessage(nullptr, [ws, user_id, peer_id, this](const std::string& msg) {
      try {
        auto json = Json::parse(msg);

        if (!ValidateMessage(ws, json, *user_id)) {
          return;
        }

        const auto& type = json["type"];
        if (type == "publish") {
          HandlePublish(ws, json, *user_id);
          // Update peer_id when a peer publishes
          if (json.contains("peer_id")) {
            *peer_id = json["peer_id"];
          }
        } else if (type == "get_streamers") {
          SendStreamers(ws, *user_id);
        } else {
          RelayMessage(json, *user_id, *peer_id);
        }
      } catch (const std::exception& e) {
        LOGE << "Message handling error: " << e.what();
        SendError(ws, "invalid_message", e.what());
      }
    });

    ws->onOpen([]() {
    });

    ws->onClosed([user_id, peer_id, this] {
      bool is_notification_required = false;
      {
        std::lock_guard<std::mutex> lock(peersMutex_);
        if (!user_id->empty() && !peer_id->empty()) {
          if (peers_[*user_id].erase(*peer_id) != 0U) {
            is_notification_required = true;
          }
        }
      }
      if (is_notification_required) {
        NotifyPeerDisconnected(*user_id, *peer_id);
        BroadcastStreamers(*user_id);
      }
    });
  }

  void SendStreamers(const std::shared_ptr<rtc::WebSocket>& ws, const UserId& user_id) {
    Json response;
    response["type"] = "streamers";
    response["peers"] = Json::array();

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      // Collect all streamers for the user
      if (peers_.find(user_id) != peers_.end()) {
        for (const auto& [peerId, peer]: peers_[user_id]) {
          if (peer.type == PeerType::streamer) {
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
    ws->send(response.dump());
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void RelayMessage(Json& msg, const UserId& user_id, const PeerId& peer_id) {
    PeerId destination_id{};
    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      destination_id = msg.value("to", "");

      if (peers_.find(user_id) == peers_.end() || peers_[user_id].find(destination_id) == peers_[user_id].end()) {
        LOGE << "Peer " << destination_id << " not found for user " << user_id;
        return;
      }
    }

    // Forward the message to the destination peer
    if (!destination_id.empty()) {
      auto& destination_peer = peers_[user_id][destination_id];
      msg["from"] = peer_id;
      destination_peer.ws->send(msg.dump());
    }
  }

  bool ValidateMessage(std::shared_ptr<rtc::WebSocket> ws, const Json& msg, UserId& user_id) {
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
    } else {
      auto uid = verifier_->VerifyToken(token);
      if (!uid || *uid != user_id) {
        SendError(ws, "user_mismatch", "Token user mismatch");
        return false;
      }
    }
    return true;
  }

  void HandlePublish(std::shared_ptr<rtc::WebSocket> ws, const Json& msg, const UserId user_id) {
    PeerId peer_id = msg["peer_id"];
    Peer new_peer{
        .id = peer_id,
        .type = msg["kind"] == "streamer" ? PeerType::streamer : PeerType::viewer,
        .name = msg["name"],
        .ws = ws,
        .profile = msg.value("profile", Json::object())
    };

    {
      std::lock_guard<std::mutex> lock(peersMutex_);
      auto& user_peers = peers_[user_id];

      // Remove existing connection if present
      if (auto existing = user_peers.find(peer_id); existing != user_peers.end()) {
        if (existing->second.ws != ws) {
          existing->second.ws->close();
          user_peers.erase(existing);
        }
      }

      // Add new peer
      user_peers[peer_id] = std::move(new_peer);
    }

    BroadcastStreamers(user_id);
  }

  void BroadcastStreamers(const UserId& user_id) {
    Json response;
    response["type"] = "streamers";
    std::vector<std::shared_ptr<rtc::WebSocket>> viewers;

    {
      std::lock_guard<std::mutex> lock(peersMutex_);

      for (const auto& [id, peer] : peers_[user_id]) {
        if (peer.type == PeerType::streamer) {
          response["peers"].push_back({{"id", id}, {"name", peer.name}, {"profile", peer.profile}});
        }
      }

      for (const auto& [id, peer] : peers_[user_id]) {
        if (peer.type == PeerType::viewer) {
          viewers.push_back(peer.ws);
        }
      }
    }

    for (const auto& ws : viewers) {
      ws->send(response.dump());
    }
  }

  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  void NotifyPeerDisconnected(const UserId user_id, const PeerId& disconnected_peer_id) {
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
        return;
      }

      auto& user_peers = user_it->second;
      auto peer_it = user_peers.find(disconnected_peer_id);
      if (peer_it == user_peers.end()) {
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

    for (const auto& ws : targets) {
      ws->send(notification.dump());
    }
  }

  static void SendError(std::shared_ptr<rtc::WebSocket> ws, const std::string& error, const std::string& message) {
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
  uint16_t port = 8000;
  bool ssl = false;

  if (argc > 1) {
    port = std::stoi(argv[1]);
  }
  if (argc > 2) {
    ssl = std::string(argv[2]) == "ssl";
  }

  // Register signal handler
  std::signal(SIGINT, SignalHandler);

  SignalingServer server(port, ssl);

  LOGI << "Signaling server running on port " << port
            << (ssl ? " with SSL" : "");
  LOGI << "Press Ctrl+C to exit";

  // Keep the program running until SIGINT (Ctrl+C) is received
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOGI << "Server stopped";
  return 0;
}
