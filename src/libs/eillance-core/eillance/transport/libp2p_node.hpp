#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <span>

#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance_libp2p.h"

namespace eillance::transport {

class Libp2pNode final {
 public:
  explicit Libp2pNode(std::span<std::byte const> secret_key) {
    if (secret_key.size() != 32) {
      throw std::invalid_argument("Libp2pNode requires exactly a 32-byte Ed25519 secret key");
    }
    // NOLINTNEXTLINE(*-reinterpret-cast)
    node_ = eillance_libp2p_node_new(reinterpret_cast<uint8_t const*>(secret_key.data()));

    eillance_libp2p_node_set_callbacks(
        node_,
        this,
        OnPeerConnectedCallback,
        OnPeerDisconnectedCallback,
        OnMessageCallback
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
        on_message_(std::move(other.on_message_)) {
    if (node_) {
      eillance_libp2p_node_set_callbacks(
          node_,
          this,
          OnPeerConnectedCallback,
          OnPeerDisconnectedCallback,
          OnMessageCallback
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

      if (node_) {
        eillance_libp2p_node_set_callbacks(
            node_,
            this,
            OnPeerConnectedCallback,
            OnPeerDisconnectedCallback,
            OnMessageCallback
        );
      }
    }
    return *this;
  }

  void Start() const {
    if (node_ != nullptr) {
      eillance_libp2p_node_start(node_);
    }
  }
  void Stop() const {
    if (node_ != nullptr) {
      eillance_libp2p_node_stop(node_);
    }
  }

  bool SendMessage(ContactID const& peer_id, std::span<std::byte const> data) const {
    if (!node_) return false;
    std::string hex = peer_id.ToString();
    return eillance_libp2p_node_send_message(
        node_,
        hex.c_str(),
        reinterpret_cast<uint8_t const*>(data.data()),
        data.size()
    );
  }

  void Dial(ContactID const& peer_id) const {
    if (!node_) return;
    std::string hex = peer_id.ToString();
    eillance_libp2p_node_dial(node_, hex.c_str());
  }

  void AddBootstrapNode(std::string const& multiaddr) const {
    if (!node_) return;
    eillance_libp2p_node_add_bootstrap_node(node_, multiaddr.c_str());
  }

  void Listen(std::string const& multiaddr) const {
    if (!node_) return;
    eillance_libp2p_node_listen(node_, multiaddr.c_str());
  }

  void SetOnConnected(std::function<void(ContactID const&)> cb) {
    on_connected_ = std::move(cb);
  }
  void SetOnDisconnected(std::function<void(ContactID const&)> cb) {
    on_disconnected_ = std::move(cb);
  }
  void SetOnMessage(std::function<void(ContactID const&, std::span<std::byte const>)> cb) {
    on_message_ = std::move(cb);
  }

 private:
  void* node_;

  std::function<void(ContactID const&)> on_connected_;
  std::function<void(ContactID const&)> on_disconnected_;
  std::function<void(ContactID const&, std::span<std::byte const>)> on_message_;

  static void OnPeerConnectedCallback(void* context, char const* peer_id_hex) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_connected_) self->on_connected_(ContactID(std::string_view(peer_id_hex)));
  }

  static void OnPeerDisconnectedCallback(void* context, char const* peer_id_hex) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_disconnected_) self->on_disconnected_(ContactID(std::string_view(peer_id_hex)));
  }

  static void OnMessageCallback(void* context, char const* peer_id_hex, uint8_t const* data, size_t len) {
    auto* self = static_cast<Libp2pNode*>(context);
    if (self->on_message_)
      self->on_message_(
          ContactID(std::string_view(peer_id_hex)),
          std::span<std::byte const>(reinterpret_cast<std::byte const*>(data), len)
      );
  }
};

}  // namespace eillance::transport
