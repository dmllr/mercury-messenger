#pragma once

#include <stdexcept>
#include <utility>
#include <span>

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
      : node_(std::exchange(other.node_, nullptr)) {
  }

  auto operator=(Libp2pNode&& other) noexcept -> Libp2pNode& {
    if (this != &other) {
      if (node_ != nullptr) {
        eillance_libp2p_node_stop(node_);
        eillance_libp2p_node_free(node_);
      }
      node_ = std::exchange(other.node_, nullptr);
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

 private:
  void* node_;
};

}  // namespace eillance::transport
