#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/rand.h>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/channel.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::identity {

class ChannelManager {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<ChannelID, Channel> channels_;
  mutable std::mutex mutex_;

 public:
  explicit ChannelManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    ReloadChannels();
  }

  void ReloadChannels() {
    std::lock_guard<std::mutex> const lock(mutex_);
    channels_.clear();
    for (auto const& c : storage_->GetChannels()) {
      channels_[c.id] = c;
    }
  }

  void CreateChannelLocal(Channel const& channel) {
    std::lock_guard<std::mutex> const lock(mutex_);
    channels_[channel.id] = channel;
    storage_->AddChannel(channel);
  }

  [[nodiscard]] auto GetChannel(ChannelID const& id) const -> std::optional<Channel> {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto const it = channels_.find(id); it != channels_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto GetChannels() const -> std::vector<Channel> {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::vector<Channel> res;
    res.reserve(channels_.size());
    for (auto const& [k, v] : channels_) {
      res.push_back(v);
    }
    return res;
  }

  static auto GenerateChannelID() -> ChannelID {
    std::array<std::byte, ChannelID::identitySize> buf{};
    RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), static_cast<int>(buf.size()));
    return ChannelID(buf);
  }

  void AddMemberLocal(ChannelID const& cid, ContactID const& member_id, int role) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = channels_.find(cid);
    if (it != channels_.end()) {
      for (auto& m : it->second.members) {
        if (m.contact_id == member_id) {
          m.role = role;
          return;
        }
      }
      it->second.members.push_back({member_id, role});
      storage_->AddChannel(it->second);
    }
  }

  void RemoveMemberLocal(ChannelID const& cid, ContactID const& member_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = channels_.find(cid);
    if (it != channels_.end()) {
      std::erase_if(it->second.members, [&](auto const& m) { return m.contact_id == member_id; });
      storage_->AddChannel(it->second);
    }
  }
};

}  // namespace eillance::identity
