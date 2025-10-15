#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hela/eillance/logging/logging.h"
#include "hela/eillance/storage/persistent_storage.hpp"
#include <nlohmann/json.hpp>

namespace hela::eillance::identity {

enum class ContactStatus {
  PendingApproval,   // We sent a request, waiting for them to confirm.
  AwaitingApproval,  // They sent a request, we need to confirm.
  Confirmed,
};

// Helper for JSON serialization
NLOHMANN_JSON_SERIALIZE_ENUM(ContactStatus, {
                                                {ContactStatus::PendingApproval, "pending_approval"},
                                                {ContactStatus::AwaitingApproval, "awaiting_approval"},
                                                {ContactStatus::Confirmed, "confirmed"},
                                            })

struct Contact {
  std::string peer_id;
  ContactStatus status;
};

class ContactManager {
  static constexpr auto CONTACTS_FILE = "contacts.json";
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<std::string, Contact> contacts_;
  mutable std::mutex mutex_;

public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    loadContacts();
  }

  // Initiates adding a new contact.
  bool addContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.count(peer_id)) {
      LOGW << "Contact " << peer_id << " already exists.";
      return false;
    }
    contacts_[peer_id] = {peer_id, ContactStatus::PendingApproval};
    saveContacts();
    return true;
  }

  // Marks that we have received a request from a peer.
  bool receiveContactRequest(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.count(peer_id) && contacts_[peer_id].status == ContactStatus::Confirmed) {
      return false;  // Already confirmed
    }
    contacts_[peer_id] = {peer_id, ContactStatus::AwaitingApproval};
    saveContacts();
    return true;
  }

  // Confirms a contact that was awaiting our approval.
  bool confirmContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!contacts_.count(peer_id)) {
      return false;
    }
    contacts_[peer_id].status = ContactStatus::Confirmed;
    saveContacts();
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  bool markAsConfirmed(const std::string& peer_id) {
    return confirmContact(peer_id);
  }

  void removeContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    contacts_.erase(peer_id);
    saveContacts();
  }

  bool isConfirmedContact(const std::string& peer_id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);
    return (it != contacts_.end() && it->second.status == ContactStatus::Confirmed);
  }

  std::optional<Contact> getContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.count(peer_id)) {
      return contacts_.at(peer_id);
    }
    return std::nullopt;
  }

  std::vector<Contact> getContacts() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Contact> result;
    for (const auto& pair: contacts_) {
      result.push_back(pair.second);
    }
    return result;
  }

private:
  void loadContacts() {
    if (storage_->Exists(CONTACTS_FILE)) {
      auto data_opt = storage_->Load(CONTACTS_FILE);
      if (data_opt) {
        try {
          std::string json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
          nlohmann::json j = nlohmann::json::parse(json_str);
          for (const auto& item: j) {
            Contact c = {item["peer_id"], item["status"]};
            contacts_[c.peer_id] = c;
          }
        } catch (const nlohmann::json::parse_error& e) {
          LOGE << "Failed to parse contacts file: " << e.what();
        }
      }
    }
  }

  void saveContacts() {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& pair: contacts_) {
      j.push_back({{"peer_id", pair.second.peer_id}, {"status", pair.second.status}});
    }
    std::string json_str = j.dump(2);
    const auto* char_ptr = reinterpret_cast<const std::byte*>(json_str.data());
    std::vector<std::byte> data(char_ptr, char_ptr + json_str.size());
    storage_->Save(CONTACTS_FILE, data);
  }
};

}  // namespace hela::eillance::identity
