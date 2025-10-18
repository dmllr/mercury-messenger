#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include <nlohmann/json.hpp>

namespace eillance::identity {

enum class ContactStatus {
  pendingApproval,   // We sent a request, waiting for them to confirm.
  awaitingApproval,  // They sent a request, we need to confirm.
  confirmed,
};

// Helper for JSON serialization
NLOHMANN_JSON_SERIALIZE_ENUM(  // NOLINT
  ContactStatus,
  {
    {ContactStatus::pendingApproval, "pending_approval"},
    {ContactStatus::awaitingApproval, "awaiting_approval"},
    {ContactStatus::confirmed, "confirmed"},
  })

struct Contact {
  std::string peer_id;
  ContactStatus status;
};

class ContactManager {
  static constexpr auto contactsFile = "contacts.json";
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<std::string, Contact> contacts_;
  mutable std::mutex mutex_;

public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadContacts();
  }

  // Initiates adding a new contact.
  bool AddContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.contains(peer_id)) {
      LOGW << "Contact " << peer_id << " already exists.";
      return false;
    }
    contacts_[peer_id] = {.peer_id=peer_id, .status=ContactStatus::pendingApproval};
    SaveContacts();
    return true;
  }

  // Marks that we have received a request from a peer.
  bool ReceiveContactRequest(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.contains(peer_id) && contacts_[peer_id].status == ContactStatus::confirmed) {
      return false;  // Already confirmed
    }
    contacts_[peer_id] = {.peer_id=peer_id, .status=ContactStatus::awaitingApproval};
    SaveContacts();
    return true;
  }

  // Confirms a contact that was awaiting our approval.
  bool ConfirmContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!contacts_.contains(peer_id)) {
      return false;
    }
    contacts_[peer_id].status = ContactStatus::confirmed;
    SaveContacts();
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  bool MarkAsConfirmed(const std::string& peer_id) {
    return ConfirmContact(peer_id);
  }

  void RemoveContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    contacts_.erase(peer_id);
    SaveContacts();
  }

  bool IsConfirmedContact(const std::string& peer_id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto itr = contacts_.find(peer_id);
    return (itr != contacts_.end() && itr->second.status == ContactStatus::confirmed);
  }

  std::optional<Contact> GetContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.contains(peer_id)) {
      return contacts_.at(peer_id);
    }
    return std::nullopt;
  }

  std::vector<Contact> GetContacts() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Contact> result;
    result.reserve(contacts_.size());
    for (const auto& val: contacts_ | std::views::values) {
      result.push_back(val);
    }
    return result;
  }

private:
  void LoadContacts() {
    if (storage_->Exists(contactsFile)) {
      const auto data_opt = storage_->Load(contactsFile);
      if (data_opt) {
        try {
          const std::string json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
          const nlohmann::json j = nlohmann::json::parse(json_str);
          for (const auto& item: j) {
            const Contact c = {.peer_id=item["peer_id"], .status=item["status"]};
            contacts_[c.peer_id] = c;
          }
        } catch (const nlohmann::json::parse_error& e) {
          LOGE << "Failed to parse contacts file: " << e.what();
        }
      }
    }
  }

  void SaveContacts() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& val: contacts_ | std::views::values) {
      j.push_back({{"peer_id", val.peer_id}, {"status", val.status}});
    }
    const auto json_str = j.dump(2);
    const auto* char_ptr = reinterpret_cast<const std::byte*>(json_str.data());
    const std::vector<std::byte> data(char_ptr, char_ptr + json_str.size());
    if (!storage_->Save(contactsFile, data)) {
        LOGE << "Failed to save contacts file to persistent storage.";
    }
  }
};

}  // namespace eillance::identity
