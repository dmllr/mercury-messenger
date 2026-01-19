#pragma once

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <openssl/rand.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

enum class RequestStatus {
  New,        // A new, valid request that should be processed.
  Duplicate,  // A resent request for one already awaiting approval.
  Ignored     // The request was ignored (contact is already confirmed or was rejected).
};

class ContactManager {
  using Token = std::array<std::byte, 16>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<ContactID, Contact> contacts_;
  mutable std::mutex mutex_;

  struct TokenInfo {
    std::chrono::steady_clock::time_point creation_time;
  };

  std::map<Token, TokenInfo> one_time_tokens_;
  static constexpr std::chrono::seconds kTokenValidityDuration{60};


public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) { LoadContacts(); }

  // Initiates adding a new contact.
  auto AddContact(const ContactID& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::pendingApproval) {
        return true;
      }
      if (it->second.status == ContactStatus::rejected) {
        it->second.status = ContactStatus::awaitingApproval;
        storage_->UpdateContact(it->second);
        return true;
      }

      LOGW << "Contact " << peer_id.ToString() << " already exists with status " << static_cast<int>(it->second.status);
      return false;
    }
    contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::pendingApproval};

    storage_->UpdateContact(contacts_[peer_id]);
    return true;
  }

  // Marks that we have received a request from a peer.
  auto ReceiveContactRequest(const ContactID& peer_id, const std::optional<std::string>& name,
                             const std::optional<std::string>& bio,
                             const std::optional<std::string>& photo) -> RequestStatus {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed || it->second.status == ContactStatus::rejected) {
        return RequestStatus::Ignored;
      }
      if (it->second.status == ContactStatus::awaitingApproval) {
        if (name) { it->second.display_name = name; }
        if (bio) { it->second.bio = bio; }
        if (photo) { it->second.photo_base64 = photo; }
        storage_->UpdateContact(it->second);
        return RequestStatus::Duplicate;
      }

      it->second.status = ContactStatus::awaitingApproval;
      if (name) { it->second.display_name = name; }
      if (bio) { it->second.bio = bio; }
      if (photo) { it->second.photo_base64 = photo; }
      storage_->UpdateContact(it->second);
    } else {
      contacts_[peer_id] = {.peer_id = peer_id,
                            .status = ContactStatus::awaitingApproval,
                            .display_name = name,
                            .bio = bio,
                            .photo_base64 = photo};
      storage_->UpdateContact(contacts_[peer_id]);
    }
    return RequestStatus::New;
  }

  // Confirms a contact that was awaiting our approval.
  auto ConfirmContact(const ContactID& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end() || it->second.status != ContactStatus::awaitingApproval) {
      LOGW << "Cannot confirm contact " << peer_id.ToString() << "; not found or not awaiting approval.";
      return false;
    }
    it->second.status = ContactStatus::confirmed;
    storage_->UpdateContact(it->second);
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  auto MarkAsConfirmed(const ContactID& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);

    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed) {
        return true;
      }
      it->second.status = ContactStatus::confirmed;
      storage_->UpdateContact(it->second);
    } else {
      contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::confirmed};
      storage_->UpdateContact(contacts_[peer_id]);
    }
    return true;
  }

  void BlockContact(const ContactID& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status != ContactStatus::rejected) {
        it->second.status = ContactStatus::rejected;
        storage_->UpdateContact(it->second);
      }
    } else {
      contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::rejected};
      storage_->UpdateContact(contacts_[peer_id]);
    }
  }

  void DeleteContact(const ContactID& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    contacts_.erase(peer_id);
    storage_->RemoveContact(peer_id);
  }

  auto IsConfirmedContact(const ContactID& peer_id) const -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto itr = contacts_.find(peer_id);
    return (itr != contacts_.end() && itr->second.status == ContactStatus::confirmed);
  }

  auto GetContact(const ContactID& peer_id) -> std::optional<Contact> {
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

  void UpdateContactProfile(const ContactID& peer_id, const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot update profile for non-existent contact: " << peer_id.ToString();
      return;
    }
    if (name) { it->second.display_name = name; }
    if (bio) { it->second.bio = bio; }
    if (photo) { it->second.photo_base64 = photo; }
    storage_->UpdateContact(it->second);
  }

  void SetDisplayNameOverride(const ContactID& peer_id, const std::string& override_name) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot set name override for non-existent contact: " << peer_id.ToString();
      return;
    }
    std::string final_name = override_name;
    if (final_name.length() > 64) {
      final_name = final_name.substr(0, 64);
    }
    it->second.display_name_override = final_name;
    storage_->UpdateContact(it->second);
  }

  void ResetDisplayNameOverride(const ContactID& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LOGW << "Cannot reset name override for non-existent contact: " << peer_id.ToString();
      return;
    }
    if (it->second.display_name_override) {
      it->second.display_name_override.reset();
      storage_->UpdateContact(it->second);
    }
  }

  void StoreMySignatureFor(const ContactID& peer_id, const std::vector<std::byte>& signature) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      return;
    }
    it->second.my_signature_for_them = signature;
    storage_->UpdateContact(it->second);
  }

  void StoreSignatureFrom(const ContactID& peer_id, const std::vector<std::byte>& signature) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto itr = contacts_.find(peer_id);
    if (itr == contacts_.end()) {
      return;
    }
    itr->second.signature_from_them = signature;
    storage_->UpdateContact(itr->second);
  }

  auto GenerateContactToken() -> Token {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::erase_if(one_time_tokens_, [now](const auto& item) {
      return (now - item.second.creation_time) > kTokenValidityDuration;
    });

    Token token;
    RAND_bytes(reinterpret_cast<unsigned char*>(token.data()), static_cast<int>(token.size()));
    one_time_tokens_[token] = {now};
    return token;
  }

  auto ValidateAndConsumeContactToken(const std::vector<std::byte>& token_vec) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (token_vec.size() != 16) {
      return false;
    }

    Token token;
    std::copy(token_vec.begin(), token_vec.end(), token.begin());

    auto it = one_time_tokens_.find(token);
    if (it == one_time_tokens_.end()) {
      return false;
    }
    auto now = std::chrono::steady_clock::now();
    if ((now - it->second.creation_time) > kTokenValidityDuration) {
      one_time_tokens_.erase(it);
      return false;
    }
    one_time_tokens_.erase(it);
    return true;
  }

  auto UnblockContact(const ContactID& peer_id) -> bool {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = contacts_.find(peer_id);
    if (it != contacts_.end() && it->second.status == ContactStatus::rejected) {
      it->second.status = ContactStatus::awaitingApproval;
      storage_->UpdateContact(it->second);
      return true;
    }
    return false;
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
