#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <span>

#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/utils/invariant.hpp"
#include "eillance_libp2p.h"

namespace eillance::transport {

enum class NodeRole {
  bootstrapNode = EILLANCE_NODE_ROLE_BOOTSTRAP_NODE,
  desktopClient = EILLANCE_NODE_ROLE_DESKTOP_CLIENT,
  mobileClient = EILLANCE_NODE_ROLE_MOBILE_CLIENT,
  bot = EILLANCE_NODE_ROLE_BOT
};

class Libp2pNode final {
 public:
  explicit Libp2pNode(std::span<std::byte const> secret_key, NodeRole role = NodeRole::desktopClient) {
    if (secret_key.size() != 32) {
      throw std::invalid_argument("Libp2pNode requires exactly a 32-byte Ed25519 secret key");
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    node_ = eillance_libp2p_node_new(
        reinterpret_cast<uint8_t const*>(secret_key.data()),
        static_cast<eillance_libp2p_node_role>(role)
    );

    eillance_libp2p_node_set_callbacks(
        node_,
        this,
        OnPeerConnectedCallback,
        OnPeerDisconnectedCallback,
        OnMessageCallback,
        OnLogCallback,
        OnTurnRelaysDiscoveredCallback,
        OnTurnAuthResultCallback
    );
  }

  ~Libp2pNode() {
    if (node_ != nullptr) {
      eillance_libp2p_node_stop(node_);
      eillance_libp2p_node_free(node_);
    }
  }

  Libp2pNode(Libp2pNode const&) = delete;
  auto operator=(Libp2pNode const&) -> Libp2pNode& = delete;

  Libp2pNode(Libp2pNode&& other) noexcept
      : node_(std::exchange(other.node_, nullptr)),
        on_connected_(std::move(other.on_connected_)),
        on_disconnected_(std::move(other.on_disconnected_)),
        on_message_(std::move(other.on_message_)),
        on_turn_relays_discovered_(std::move(other.on_turn_relays_discovered_)),
        on_turn_auth_result_(std::move(other.on_turn_auth_result_)) {
    if (node_ != nullptr) {
      eillance_libp2p_node_set_callbacks(
          node_,
          this,
          OnPeerConnectedCallback,
          OnPeerDisconnectedCallback,
          OnMessageCallback,
          OnLogCallback,
          OnTurnRelaysDiscoveredCallback,
          OnTurnAuthResultCallback
      );
    }
  }

  auto operator=(Libp2pNode&& other) noexcept -> Libp2pNode& {
    if (this != &other) {
      if (node_ != nullptr) {
        eillance_libp2p_node_stop(node_);
        eillance_libp2p_node_free(node_);
      }
      node_ = std::exchange(other.node_, nullptr);
      on_connected_ = std::move(other.on_connected_);
      on_disconnected_ = std::move(other.on_disconnected_);
      on_message_ = std::move(other.on_message_);
      on_turn_relays_discovered_ = std::move(other.on_turn_relays_discovered_);
      on_turn_auth_result_ = std::move(other.on_turn_auth_result_);

      if (node_ != nullptr) {
        eillance_libp2p_node_set_callbacks(
            node_,
            this,
            OnPeerConnectedCallback,
            OnPeerDisconnectedCallback,
            OnMessageCallback,
            OnLogCallback,
            OnTurnRelaysDiscoveredCallback,
            OnTurnAuthResultCallback
        );
      }
    }
    return *this;
  }

  void Start() const {
    INVARIANT_NOT_NULL(node_);
    eillance_libp2p_node_start(node_);
  }
  void Stop() const {
    if (node_ != nullptr) {
      eillance_libp2p_node_stop(node_);
    }
  }

  void Disconnect(ContactID const& peer_id) const {
    INVARIANT_NOT_NULL(node_);
    std::string const hex = peer_id.ToString();
    eillance_libp2p_node_disconnect(node_, hex.c_str());
  }

  bool SendChat(ContactID const& peer_id, std::span<std::byte const> data) const {
    INVARIANT_NOT_NULL(node_);
    std::string const hex = peer_id.ToString();
    return eillance_libp2p_node_send_chat(
        node_,
        hex.c_str(),
        reinterpret_cast<uint8_t const*>(data.data()),
        data.size()
    );
  }

  bool SendFile(ContactID const& peer_id, std::span<std::byte const> data) const {
    INVARIANT_NOT_NULL(node_);
    std::string const hex = peer_id.ToString();
    return eillance_libp2p_node_send_file(
        node_,
        hex.c_str(),
        reinterpret_cast<uint8_t const*>(data.data()),
        data.size()
    );
  }

  void Dial(ContactID const& peer_id) const {
    INVARIANT_NOT_NULL(node_);
    std::string const hex = peer_id.ToString();
    eillance_libp2p_node_dial(node_, hex.c_str());
  }

  void AddBootstrapNode(std::string const& multiaddr) const {
    INVARIANT_NOT_NULL(node_);
    eillance_libp2p_node_add_bootstrap_node(node_, multiaddr.c_str());
  }

  void Listen(std::string const& multiaddr) const {
    INVARIANT_NOT_NULL(node_);
    eillance_libp2p_node_listen(node_, multiaddr.c_str());
  }

  void DiscoverTurnRelays() const {
    INVARIANT_NOT_NULL(node_);
    eillance_libp2p_node_discover_turn_relays(node_);
  }

  void AuthTurnRelay(ContactID const& peer_id) const {
    INVARIANT_NOT_NULL(node_);
    std::string const hex = peer_id.ToString();
    eillance_libp2p_node_auth_turn_relay(node_, hex.c_str());
  }

  void SetOnConnected(std::function<void(ContactID const&, bool)> cb) {
    on_connected_ = std::move(cb);
  }
  void SetOnDisconnected(std::function<void(ContactID const&)> cb) {
    on_disconnected_ = std::move(cb);
  }
  void SetOnMessage(std::function<void(ContactID const&, std::span<std::byte const>)> cb) {
    on_message_ = std::move(cb);
  }
  void SetOnTurnRelaysDiscovered(std::function<void(std::vector<ContactID> const&)> cb) {
    on_turn_relays_discovered_ = std::move(cb);
  }
  void SetOnTurnAuthResult(std::function<void(ContactID const&, bool, std::string const&, uint16_t, std::string const&, std::string const&)> cb) {
    on_turn_auth_result_ = std::move(cb);
  }

 private:
  void* node_;

  std::function<void(ContactID const&, bool)> on_connected_;
  std::function<void(ContactID const&)> on_disconnected_;
  std::function<void(ContactID const&, std::span<std::byte const>)> on_message_;
  std::function<void(std::vector<ContactID> const&)> on_turn_relays_discovered_;
  std::function<void(ContactID const&, bool, std::string const&, uint16_t, std::string const&, std::string const&)> on_turn_auth_result_;

  static void OnPeerConnectedCallback(void* context, char const* peer_id_hex, bool is_circuit) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_connected_) {
      self->on_connected_(ContactID(std::string_view(peer_id_hex)), is_circuit);
    }
  }

  static void OnPeerDisconnectedCallback(void* context, char const* peer_id_hex) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_disconnected_) {
      self->on_disconnected_(ContactID(std::string_view(peer_id_hex)));
    }
  }

  static void OnMessageCallback(void* context, char const* peer_id_hex, uint8_t const* data, size_t len) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_message_) {
      self->on_message_(
          ContactID(std::string_view(peer_id_hex)),
          std::span<std::byte const>(reinterpret_cast<std::byte const*>(data), len)
      );
    }
  }

  static void OnLogCallback(void* /*context*/, int level, char const* message) {
    switch (level) {
      case 1:
        LGE << "[libp2p] " << message;
        break;
      case 2:
        LGW << "[libp2p] " << message;
        break;
      case 3:
        LGI << "[libp2p] " << message;
        break;
      default:
        LGD << "[libp2p] " << message;
        break;
    }
  }

  static void OnTurnRelaysDiscoveredCallback(void* context, char const* providers_csv) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_turn_relays_discovered_ && providers_csv != nullptr) {
      std::vector<ContactID> relays;
      std::string csv(providers_csv);
      size_t start = 0;
      size_t end = csv.find(',');
      while (end != std::string::npos) {
        if (end > start) relays.emplace_back(csv.substr(start, end - start));
        start = end + 1;
        end = csv.find(',', start);
      }
      if (start < csv.length()) {
        relays.emplace_back(csv.substr(start));
      }
      self->on_turn_relays_discovered_(relays);
    }
  }

  static void OnTurnAuthResultCallback(void* context, char const* peer_id_hex, char const* ip, uint16_t port, char const* user, char const* cred) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_turn_auth_result_ && peer_id_hex != nullptr) {
      ContactID const peer(std::string_view(peer_id_hex));
      if (ip == nullptr || user == nullptr || cred == nullptr) {
        self->on_turn_auth_result_(peer, false, "", 0, "", "");
      } else {
        self->on_turn_auth_result_(peer, true, ip, port, user, cred);
      }
    }
  }
};

}  // namespace eillance::transport
