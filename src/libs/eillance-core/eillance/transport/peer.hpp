#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::transport {

struct Peer {
  using Json = nlohmann::json;

  explicit Peer(ContactID const& id);

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  ContactID const id;

  std::atomic<bool> data_channel_is_open{false};
  std::atomic<bool> data_channel_initialized{false};
  std::atomic<bool> is_connecting{false};
  std::atomic<bool> is_circuit{false};
  std::atomic<Time::Unit> last_activity_time{0};
  std::atomic<Time::Unit> last_connect_attempt{0};
  std::atomic<bool> crcp_verified{false};
  std::atomic<bool> has_media_peer_connection{false};
  std::atomic<bool> legacy_prompt_sent{false};
  std::unique_ptr<crypto::SessionCipher> session_cipher{};

  std::optional<std::string> pending_offer_sdp;

  std::atomic<uint64_t> tx_seq{0};
  std::atomic<uint64_t> rx_seq{0};
  std::map<uint64_t, Json> out_of_order_queue;
  std::mutex queue_mutex;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

inline Peer::Peer(ContactID const& id)
    : id(id),
      last_activity_time(Time::SteadyNow()),
      last_connect_attempt(0) {
  session_cipher = std::make_unique<crypto::SessionCipher>();
}

}  // namespace eillance::transport
