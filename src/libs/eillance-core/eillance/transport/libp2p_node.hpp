#pragma once

#include <utility>

#include "eillance_libp2p.h"

namespace eillance::transport {

class Libp2pNode final {
 public:
  Libp2pNode()
      : node_(eillance_libp2p_node_new()) {
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
    if (node_ != nullptr) eillance_libp2p_node_start(node_);
  }
  void Stop() const {
    if (node_ != nullptr) eillance_libp2p_node_stop(node_);
  }

 private:
  void* node_;
};

}  // namespace eillance::transport
