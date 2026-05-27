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

#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

enum class RequestStatus {
  New,        // A new, valid request that should be processed.
  duplicate,  // A resent request for one already awaiting approval.
  ignored     // The request was ignored (contact is already confirmed or was rejected).
};

class ContactManager {
  static constexpr size_t tokenSize = 16;
  using Token = std::array<std::byte, tokenSize>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<ContactID, Contact> contacts_{};
  mutable std::mutex mutex_;

  struct TokenInfo {
    std::chrono::steady_clock::time_point creation_time;
  };

  std::map<Token, TokenInfo> oneTimeTokens_{};
  static constexpr std::chrono::seconds tokenValidityDuration{60};

 public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadContacts();
  }

  // Reloads contacts from storage (useful for debug actions that modify DB directly)
  void ReloadContacts() {
    std::lock_guard<std::mutex> const lock(mutex_);
    contacts_.clear();
    LoadContactsInternal();
  }

  // Clears in-memory contacts (useful for debug erase)
  void ClearContacts() {
    std::lock_guard<std::mutex> const lock(mutex_);
    contacts_.clear();
  }

  // Initiates adding a new contact.
  auto AddContact(ContactID const& peer_id) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::pendingApproval) {
        return true;
      }
      if (it->second.status == ContactStatus::rejected) {
        it->second.status = ContactStatus::awaitingApproval;
        storage_->UpdateContact(it->second);
        return true;
      }

      LGW << "Contact " << peer_id.ToString() << " already exists with status " << static_cast<int>(it->second.status);
      return false;
    }
    contacts_[peer_id] = {.peer_id = peer_id, .status = ContactStatus::pendingApproval};

    storage_->UpdateContact(contacts_[peer_id]);
    return true;
  }

  // Marks that we have received a request from a peer.
  auto ReceiveContactRequest(ContactID const& peer_id, ProfileUpdate const& profile) -> RequestStatus {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = contacts_.find(peer_id);
    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed || it->second.status == ContactStatus::rejected) {
        return RequestStatus::ignored;
      }
      if (it->second.status == ContactStatus::awaitingApproval) {
        if (profile.display_name) {
          it->second.display_name = profile.display_name;
        }
        if (profile.bio) {
          it->second.bio = profile.bio;
        }
        if (profile.photo_base64) {
          it->second.photo_base64 = profile.photo_base64;
        }
        storage_->UpdateContact(it->second);
        return RequestStatus::duplicate;
      }

      it->second.status = ContactStatus::awaitingApproval;
      if (profile.display_name) {
        it->second.display_name = profile.display_name;
      }
      if (profile.bio) {
        it->second.bio = profile.bio;
      }
      if (profile.photo_base64) {
        it->second.photo_base64 = profile.photo_base64;
      }
      storage_->UpdateContact(it->second);
    } else {
      contacts_[peer_id] = {
          .peer_id = peer_id,
          .status = ContactStatus::awaitingApproval,
          .display_name = profile.display_name,
          .bio = profile.bio,
          .photo_base64 = profile.photo_base64
      };
      storage_->UpdateContact(contacts_[peer_id]);
    }
    return RequestStatus::New;
  }

  // Confirms a contact that was awaiting our approval.
  auto ConfirmContact(ContactID const& peer_id) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end() ||
        (it->second.status != ContactStatus::awaitingApproval && it->second.status != ContactStatus::pendingApproval)) {
      LGW << "Cannot confirm contact " << peer_id.ToString() << "; not found or not awaiting approval.";
      return false;
    }
    it->second.status = ContactStatus::confirmed;
    storage_->UpdateContact(it->second);
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  auto MarkAsConfirmed(ContactID const& peer_id) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto it = contacts_.find(peer_id);

    if (it != contacts_.end()) {
      if (it->second.status == ContactStatus::confirmed) {
        return true;
      }
      if (it->second.status != ContactStatus::pendingApproval) {
        return false;
      }
      it->second.status = ContactStatus::confirmed;
      storage_->UpdateContact(it->second);
    } else {
      return false;
    }
    return true;
  }

  void BlockContact(ContactID const& peer_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
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

  void DeleteContact(ContactID const& peer_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
    contacts_.erase(peer_id);
    storage_->RemoveContact(peer_id);
  }

  [[nodiscard]] auto IsConfirmedContact(ContactID const& peer_id) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    return (it != contacts_.end() && it->second.status == ContactStatus::confirmed);
  }

  [[nodiscard]] auto GetContact(ContactID const& peer_id) -> std::optional<Contact> {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (contacts_.contains(peer_id)) {
      return contacts_.at(peer_id);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto GetContacts() -> std::vector<Contact> {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::vector<Contact> result;
    result.reserve(contacts_.size());
    for (auto const& val : contacts_ | std::views::values) {
      result.push_back(val);
    }
    return result;
  }

  void UpdateContactProfile(ContactID const& peer_id, ProfileUpdate const& profile) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LGW << "Cannot update profile for non-existent contact: " << peer_id.ToString();
      return;
    }
    if (profile.display_name) {
      it->second.display_name = profile.display_name;
    }
    if (profile.bio) {
      it->second.bio = profile.bio;
    }
    if (profile.photo_base64) {
      it->second.photo_base64 = profile.photo_base64;
    }
    storage_->UpdateContact(it->second);
  }

  void SetDisplayNameOverride(ContactID const& peer_id, std::string const& override_name) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LGW << "Cannot set name override for non-existent contact: " << peer_id.ToString();
      return;
    }
    constexpr size_t max_name_len = 64;
    std::string final_name = override_name;
    if (final_name.length() > max_name_len) {
      final_name = final_name.substr(0, max_name_len);
    }
    it->second.display_name_override = final_name;
    storage_->UpdateContact(it->second);
  }

  void ResetDisplayNameOverride(ContactID const& peer_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      LGW << "Cannot reset name override for non-existent contact: " << peer_id.ToString();
      return;
    }
    if (it->second.display_name_override) {
      it->second.display_name_override.reset();
      storage_->UpdateContact(it->second);
    }
  }

  void StoreMySignatureFor(ContactID const& peer_id, std::vector<std::byte> const& signature) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      return;
    }
    it->second.my_signature_for_them = signature;
    storage_->UpdateContact(it->second);
  }

  void StoreSignatureFrom(ContactID const& peer_id, std::vector<std::byte> const& signature) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const it = contacts_.find(peer_id);
    if (it == contacts_.end()) {
      return;
    }
    it->second.signature_from_them = signature;
    storage_->UpdateContact(it->second);
  }

  auto GenerateContactToken() -> Token {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::erase_if(oneTimeTokens_, [now](auto const& item) {
      return (now - item.second.creation_time) > tokenValidityDuration;
    });

    Token token;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    RAND_bytes(reinterpret_cast<unsigned char*>(token.data()), static_cast<int>(token.size()));
    oneTimeTokens_[token] = {now};
    return token;
  }

  auto ValidateAndConsumeContactToken(std::vector<std::byte> const& token_vec) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (token_vec.size() != tokenSize) {
      return false;
    }

    Token token;
    std::ranges::copy(token_vec, token.begin());

    auto const itr = oneTimeTokens_.find(token);
    if (itr == oneTimeTokens_.end()) {
      return false;
    }
    auto const now = std::chrono::steady_clock::now();
    if (now - itr->second.creation_time <= tokenValidityDuration) {
      oneTimeTokens_.erase(itr);
      return true;
    }
    return false;
  }

  auto UnblockContact(ContactID const& peer_id) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto const itr = contacts_.find(peer_id);
        itr != contacts_.end() && itr->second.status == ContactStatus::rejected) {
      itr->second.status = ContactStatus::awaitingApproval;
      storage_->UpdateContact(itr->second);
      return true;
    }
    return false;
  }

 private:
  void LoadContacts() {
    std::lock_guard<std::mutex> const lock(mutex_);
    LoadContactsInternal();
  }

  void LoadContactsInternal() {
    auto const contacts_from_db = storage_->GetContacts();
    for (auto const& contact : contacts_from_db) {
      contacts_[contact.peer_id] = contact;
    }
  }
};

}  // namespace eillance::identity
