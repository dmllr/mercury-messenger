#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::transport {

struct Peer {
  explicit Peer(ContactID const& id);

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  ContactID const id;

  std::atomic<bool> data_channel_is_open{false};
  std::atomic<Time::Unit> last_activity_time{0};
  std::atomic<bool> has_media_peer_connection{false};
  std::atomic<bool> legacy_prompt_sent{false};
  std::unique_ptr<crypto::SessionCipher> session_cipher{};

  std::optional<std::string> pending_offer_sdp;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

inline Peer::Peer(ContactID const& id)
    : id(id),
      last_activity_time(Time::Now()) {
  session_cipher = std::make_unique<crypto::SessionCipher>();
}

}  // namespace eillance::transport
