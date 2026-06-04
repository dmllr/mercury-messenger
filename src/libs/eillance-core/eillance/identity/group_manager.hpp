#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/rand.h>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/group.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::identity {

class GroupManager {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<GroupID, Group> groups_;
  mutable std::mutex mutex_;

 public:
  explicit GroupManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    ReloadGroups();
  }

  void ReloadGroups() {
    std::lock_guard<std::mutex> const lock(mutex_);
    groups_.clear();
    for (auto const& g : storage_->GetGroups()) {
      groups_[g.id] = g;
    }
  }

  void CreateGroupLocal(Group const& group) {
    std::lock_guard<std::mutex> const lock(mutex_);
    groups_[group.id] = group;
    storage_->AddGroup(group);
  }

  [[nodiscard]] auto GetGroup(GroupID const& id) const -> std::optional<Group> {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto const it = groups_.find(id); it != groups_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  [[nodiscard]] auto GetGroups() const -> std::vector<Group> {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::vector<Group> res;
    res.reserve(groups_.size());
    for (auto const& [k, v] : groups_) {
      res.push_back(v);
    }
    return res;
  }

  static auto GenerateGroupID() -> GroupID {
    std::array<std::byte, GroupID::identitySize> buf{};
    // NOLINTNEXTLINE(*-reinterpret-cast)
    RAND_bytes(reinterpret_cast<unsigned char*>(buf.data()), static_cast<int>(buf.size()));
    return GroupID(buf);
  }

  void AddMemberLocal(GroupID const& gid, ContactID const& member_id, int role) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = groups_.find(gid);
    if (it != groups_.end()) {
      for (auto& m : it->second.members) {
        if (m.contact_id == member_id) {
          m.role = role;
          return;
        }
      }
      it->second.members.push_back({member_id, role});
      storage_->AddGroup(it->second);
    }
  }

  void RemoveMemberLocal(GroupID const& gid, ContactID const& member_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = groups_.find(gid);
    if (it != groups_.end()) {
      std::erase_if(it->second.members, [&](auto const& m) { return m.contact_id == member_id; });
      storage_->AddGroup(it->second);
    }
  }

  void ProjectGroupState(messaging::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    std::string const type = json.value("type", "");
    if (type == "group_create") {
      Group g;
      g.id = GroupID(entry.topic_id.Span());
      g.name = json.value("name", "");
      g.owner_id = ContactID(json.value("owner_id", ""));
      if (json.contains("members")) {
        for (auto const& m_json : json["members"]) {
          g.members.push_back({ContactID(m_json.value("contact_id", "")), m_json.value("role", 0)});
        }
      }
      CreateGroupLocal(g);
      EventQueue::Instance().Push(EventGroupsStateUpdated{});
    } else if (type == "group_member_add") {
      auto g = GetGroup(GroupID(entry.topic_id.Span()));
      if (g && g->owner_id == entry.author_id) {
        AddMemberLocal(g->id, ContactID(json.value("contact_id", "")), json.value("role", 0));
        EventQueue::Instance().Push(EventGroupsStateUpdated{});
      }
    } else if (type == "group_member_remove") {
      auto g = GetGroup(GroupID(entry.topic_id.Span()));
      ContactID target(json.value("contact_id", ""));
      if (g && (g->owner_id == entry.author_id || target == entry.author_id)) {
        RemoveMemberLocal(g->id, target);
        EventQueue::Instance().Push(EventGroupsStateUpdated{});
      }
    }
  }
};

}  // namespace eillance::identity
