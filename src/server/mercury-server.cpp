#include "eillance/crypto/crypto.hpp"
#include <chrono>
#include <csignal>
#include <memory>
#include <mutex>
#include <openssl/rand.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>

#include "eillance/utils/args_parser.hpp"

constexpr auto logInstanceId = 454;
#define LGV PLOG_(logInstanceId, plog::verbose)  // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGD PLOG_(logInstanceId, plog::debug)    // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGI PLOG_(logInstanceId, plog::info)     // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGW PLOG_(logInstanceId, plog::warning)  // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGE PLOG_(logInstanceId, plog::error)    // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)

class SignalingServer {
public:
  using Json = nlohmann::json;
  using PeerId = std::string;

  explicit SignalingServer(const rtc::WebSocketServer::Configuration& config) {
    server_ = std::make_unique<rtc::WebSocketServer>(config);
    server_->onClient([this](const std::shared_ptr<rtc::WebSocket>& wss) { HandleConnection(wss); });
  }

private:
  struct Peer {
    std::shared_ptr<rtc::WebSocket> ws;
  };

  std::unique_ptr<rtc::WebSocketServer> server_;
  std::unordered_map<PeerId, Peer> peers_;  // Authenticated peers
  std::unordered_map<rtc::WebSocket*, std::string> challenges_;
  std::mutex peers_mutex_;

  // Generates a cryptographically secure random string to be used as a challenge.
  static auto GenerateChallenge() -> std::string {
    std::vector<std::byte> buffer(32);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(buffer.data()), static_cast<int>(buffer.size())) != 1) { throw std::runtime_error("Failed to generate random bytes for challenge."); }
    return eillance::crypto::ToHex(buffer);
  }

  void HandleConnection(const std::shared_ptr<rtc::WebSocket>& wss) {
    LGD << "New connection established";
    auto peer_id = std::make_shared<PeerId>();

    wss->onMessage(nullptr, [this, wss, peer_id](const std::string& msg) { HandleMessage(wss, msg, *peer_id); });

    wss->onClosed([this, wss, peer_id]() { HandleDisconnection(wss.get(), *peer_id); });
  }

  void HandleMessage(const std::shared_ptr<rtc::WebSocket>& wss, const std::string& msg, PeerId& peer_id) {
    try {
      LGV << "<< " << msg;
      const auto json = Json::parse(msg);
      const auto type = json.value("type", "");

      if (peer_id.empty()) {
        if (type == "login" && json.contains("id")) {
          const PeerId temp_id = json["id"].get<PeerId>();
          if (temp_id.empty()) {
            SendError(wss, "invalid_id", "Peer ID cannot be empty.");
            return;
          }

          const std::string challenge = GenerateChallenge();
          {
            const std::lock_guard<std::mutex> lock(peers_mutex_);
            challenges_[wss.get()] = challenge;
          }

          const Json response = {{"type", "auth_challenge"}, {"challenge", challenge}};
          wss->send(response.dump());

        } else if (type == "auth_response" && json.contains("signature") && json.contains("id")) {
          const PeerId claimed_id = json["id"].get<PeerId>();
          std::string challenge;
          {
            const std::lock_guard<std::mutex> lock(peers_mutex_);
            const auto it = challenges_.find(wss.get());
            if (it == challenges_.end()) {
              SendError(wss, "unexpected_auth_response", "No pending challenge for this connection.");
              wss->close();
              return;
            }
            challenge = it->second;
          }

          const auto signature_hex = json["signature"].get<std::string>();
          const auto signature_bytes = eillance::crypto::FromHex(signature_hex);

          if (eillance::crypto::Identity::Verify(claimed_id, challenge, signature_bytes)) {
            LGI << "Peer authenticated: " << claimed_id;
            peer_id = claimed_id;

            {
              const std::lock_guard<std::mutex> lock(peers_mutex_);
              challenges_.erase(wss.get());

              if (auto itr = peers_.find(peer_id); itr != peers_.end()) {
                LGI << "Peer " << peer_id << " is reconnecting. Closing old connection.";
                itr->second.ws->close();
              }
              peers_[peer_id] = {wss};
            }

            const Json success_msg = {{"type", "login_success"}};
            wss->send(success_msg.dump());

          } else {
            LGE << "Authentication failed for peer " << claimed_id;
            SendError(wss, "auth_failed", "Signature verification failed.");
            wss->close();
          }
        } else {
          SendError(wss, "login_or_auth_required", "Connection not authenticated.");
          wss->close();
        }
      } else {
        if (!json.contains("to")) {
          SendError(wss, "missing_recipient", "Message must have a 'to' field for relaying.");
          return;
        }
        const auto destination_id = json.value("to", "");
        if (destination_id.empty()) {
          SendError(wss, "empty_recipient", "Recipient 'to' field cannot be empty.");
          return;
        }

        LGD << "Relaying message from: " << peer_id << " to: " << destination_id.c_str();

        {
          const std::lock_guard<std::mutex> lock(peers_mutex_);
          const auto itr = peers_.find(destination_id);
          if (itr == peers_.end() || !itr->second.ws || !itr->second.ws->isOpen()) {
            LGE << "Peer " << destination_id.c_str() << " not found or not connected.";
            SendError(wss, "peer_not_found", "Peer '" + std::string(destination_id) + "' not found.", destination_id);
            return;
          }

          auto relayed_json = json;
          relayed_json["from"] = peer_id;
          const std::string message_str = relayed_json.dump();
          LGV << ">> to " << destination_id.c_str() << ": " << message_str;
          itr->second.ws->send(message_str);
        }
      }
    } catch (const std::exception& e) {
      SendError(wss, "invalid_message", e.what());
    }
  }

  void HandleDisconnection(rtc::WebSocket* wss, const PeerId& peer_id) {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    if (peer_id.empty()) {
      LGD << "Unauthenticated peer disconnected.";
      challenges_.erase(wss);
    } else {
      const auto it = peers_.find(peer_id);
      if (it != peers_.end() && it->second.ws.get() == wss) {
        peers_.erase(it);
        LGI << "Peer disconnected and removed: " << peer_id;
      } else {
        LGI << "Stale connection for peer " << peer_id << " closed.";
      }
    }
  }

  static void SendError(const std::shared_ptr<rtc::WebSocket>& wss, const std::string& error, const std::string& message, const std::optional<PeerId>& for_peer = std::nullopt) {
    if (!wss || !wss->isOpen()) { return; }
    LGE << "Sending error: " << error << " - " << message;
    Json response = {{"type", "error"}, {"error", error}, {"message", message}};

    if (for_peer) { response["for_peer"] = *for_peer; }
    wss->send(response.dump());
  }
};

namespace {

volatile std::sig_atomic_t signalReceived = 0;  // NOLINT(*-avoid-non-const-global-variables)

void SignalHandler(int signum) {
  LGI << "\nInterrupt signal (" << signum << ") received.\n";
  signalReceived = signum;
}

constexpr auto ParseLogLevel(std::string_view level) -> plog::Severity {
  if (level == "none") { return plog::none; }
  if (level == "fatal") { return plog::fatal; }
  if (level == "error") { return plog::error; }
  if (level == "warning") { return plog::warning; }
  if (level == "info") { return plog::info; }
  if (level == "debug") { return plog::debug; }
  if (level == "verbose") { return plog::verbose; }
  return plog::info;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  const eillance::utils::ArgsParser args(argc, argv);

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
  if (port_arg.has_value()) { config.port = static_cast<uint16_t>(port_arg.value()); }

  config.certificatePemFile = args.Get("--ssl-cert");
  config.keyPemFile = args.Get("--ssl-cert-key");
  if (config.certificatePemFile.has_value() && config.keyPemFile.has_value()) { config.enableTls = true; }

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
