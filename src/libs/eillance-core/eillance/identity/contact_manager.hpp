#pragma once

#include <memory>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

class ContactManager {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<std::string, Contact> contacts_;
  mutable std::mutex mutex_;
  
  struct TokenInfo {
    std::chrono::steady_clock::time_point creation_time;
  };
  std::unordered_map<std::string, TokenInfo> one_time_tokens_;
  static constexpr std::chrono::seconds kTokenValidityDuration{60};


public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) { LoadContacts(); }

  // Initiates adding a new contact.
  auto AddContact(const std::string& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      // If the contact exists and is pending, we allow the "add" operation
      // to succeed so the caller can re-send the request.
      if (it->second.status == ContactStatus::pendingApproval) { return true; }
      LOGW << "Contact " << peer_id << " already exists.";
      return false;
    }
    contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::pendingApproval};

    storage_->UpdateContact(contacts_[peer_id]);
    return true;
  }

  // Marks that we have received a request from a peer.
  auto ReceiveContactRequest(const std::string& peer_id, const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed) {
        return false;  // Already confirmed
      }
      it->second.status = ContactStatus::awaitingApproval;
      if (name) { it->second.display_name = name; }
      if (bio) { it->second.bio = bio; }
      if (photo) { it->second.photo_base64 = photo; }
      storage_->UpdateContact(it->second);
    } else {
      contacts_[peer_id] = {
          .peer_id = peer_id,
          .status = ContactStatus::awaitingApproval,
          .display_name = name,
          .bio = bio,
          .photo_base64 = photo,
      };
      storage_->UpdateContact(contacts_[peer_id]);
    }
    return true;
  }

  // Confirms a contact that was awaiting our approval.
  auto ConfirmContact(const std::string& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end() || it->second.status != ContactStatus::awaitingApproval) {
      LOGW << "Cannot confirm contact " << peer_id << "; not found or not awaiting approval.";
      return false;
    }
    it->second.status = ContactStatus::confirmed;
    storage_->UpdateContact(it->second);
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  auto MarkAsConfirmed(const std::string& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);

    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed) {
        return true;  // Already confirmed, treat as success but no state change.
      }
      // If contact exists, just update its status.
      it->second.status = ContactStatus::confirmed;
      storage_->UpdateContact(it->second);
    } else {
      // If we receive a confirmation for a contact that doesn't exist (e.g., after
      // a clear data or re-confirmation), create it.
      contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::confirmed};
      storage_->UpdateContact(contacts_[peer_id]);
    }
    return true;
  }

  void RemoveContact(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    contacts_.erase(peer_id);
    storage_->RemoveContact(peer_id);
  }

  auto IsConfirmedContact(const std::string& peer_id) const -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto itr = contacts_.find(peer_id);
    return (itr != contacts_.end() && itr->second.status == ContactStatus::confirmed);
  }

  auto GetContact(const std::string& peer_id) -> std::optional<Contact> {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (contacts_.contains(peer_id)) { return contacts_.at(peer_id); }
    return std::nullopt;
  }

  auto GetContacts() const -> std::vector<Contact> {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Contact> result;
    result.reserve(contacts_.size());
    for (const auto& val: contacts_ | std::views::values) {
      result.push_back(val);
    }
    return result;
  }

  void UpdateContactProfile(const std::string& peer_id, const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot update profile for non-existent contact: " << peer_id;
      return;
    }
    if (name) { it->second.display_name = name; }
    if (bio) { it->second.bio = bio; }
    if (photo) { it->second.photo_base64 = photo; }
    storage_->UpdateContact(it->second);
  }

  void SetDisplayNameOverride(const std::string& peer_id, const std::string& override_name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot set name override for non-existent contact: " << peer_id;
      return;
    }
    std::string final_name = override_name;
    if (final_name.length() > 64) {
      final_name = final_name.substr(0, 64);
    }
    it->second.display_name_override = final_name;
    storage_->UpdateContact(it->second);
  }

  void ResetDisplayNameOverride(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot reset name override for non-existent contact: " << peer_id;
      return;
    }
    if (it->second.display_name_override) {
      it->second.display_name_override.reset();
      storage_->UpdateContact(it->second);
    }
  }

  void StoreMySignatureFor(const std::string& peer_id, const std::string& signature_hex) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      return;
    }
    it->second.my_signature_for_them = signature_hex;
    storage_->UpdateContact(it->second);
  }

  void StoreSignatureFrom(const std::string& peer_id, const std::string& signature_hex) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      return;
    }
    it->second.signature_from_them = signature_hex;
    storage_->UpdateContact(it->second);
  }

  auto GenerateContactToken() -> std::string {
    const std::lock_guard<std::mutex> lock(mutex_);
    // Cleanup expired tokens to prevent unbounded growth.
    auto now = std::chrono::steady_clock::now();
    std::erase_if(one_time_tokens_, [now](const auto& item) {
      return (now - item.second.creation_time) > kTokenValidityDuration;
    });

    std::vector<std::byte> token_bytes(16);
    RAND_bytes(reinterpret_cast<unsigned char*>(token_bytes.data()), static_cast<int>(token_bytes.size()));
    std::string token_hex = crypto::ToHex(token_bytes);
    one_time_tokens_[token_hex] = {now};
    return token_hex;
  }

  auto ValidateAndConsumeContactToken(const std::string& token) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = one_time_tokens_.find(token);
    if (it == one_time_tokens_.end()) {
      return false;  // Token not found
    }
    auto now = std::chrono::steady_clock::now();
    if ((now - it->second.creation_time) > kTokenValidityDuration) {
      one_time_tokens_.erase(it);
      return false;  // Token expired
    }
    one_time_tokens_.erase(it);  // Valid token, consume it
    return true;
  }

private:
  void LoadContacts() {
    const auto contacts_from_db = storage_->GetContacts();
    for (const auto& contact: contacts_from_db) {
      contacts_[contact.peer_id] = contact;
    }
  }
};

}  // namespace eillance::identity
