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
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

enum class RequestStatus {
  New,        // A new, valid request that should be processed.
  duplicate,  // A resent request for one already awaiting approval.
  ignored     // The request was ignored (contact is already confirmed or was rejected).
};

class ContactManager {
  static constexpr size_t kTokenSize = 16;
  using Token = std::array<std::byte, kTokenSize>;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<ContactID, Contact> contacts_{};
  mutable std::mutex mutex_;

  struct TokenInfo {
    std::chrono::steady_clock::time_point creationTime;
  };

  std::map<Token, TokenInfo> oneTimeTokens_{};
  static constexpr std::chrono::seconds kTokenValidityDuration{60};

 public:
  explicit ContactManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadContacts();
  }

  // Initiates adding a new contact.
  auto AddContact(ContactID const& peerId) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter != contacts_.end()) {
      if (iter->second.status == ContactStatus::pendingApproval) {
        return true;
      }
      if (iter->second.status == ContactStatus::rejected) {
        iter->second.status = ContactStatus::awaitingApproval;
        storage_->UpdateContact(iter->second);
        return true;
      }

      LGW << "Contact " << peerId.ToString() << " already exists with status " << static_cast<int>(iter->second.status);
      return false;
    }
    contacts_[peerId] = {.peer_id = peerId, .status = ContactStatus::pendingApproval};

    storage_->UpdateContact(contacts_[peerId]);
    return true;
  }

  // Marks that we have received a request from a peer.
  auto ReceiveContactRequest(
      ContactID const& peerId,
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  ) -> RequestStatus {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto iter = contacts_.find(peerId);
    if (iter != contacts_.end()) {
      if (iter->second.status == ContactStatus::confirmed || iter->second.status == ContactStatus::rejected) {
        return RequestStatus::ignored;
      }
      if (iter->second.status == ContactStatus::awaitingApproval) {
        if (name) {
          iter->second.display_name = name;
        }
        if (bio) {
          iter->second.bio = bio;
        }
        if (photo) {
          iter->second.photo_base64 = photo;
        }
        storage_->UpdateContact(iter->second);
        return RequestStatus::duplicate;
      }

      iter->second.status = ContactStatus::awaitingApproval;
      if (name) {
        iter->second.display_name = name;
      }
      if (bio) {
        iter->second.bio = bio;
      }
      if (photo) {
        iter->second.photo_base64 = photo;
      }
      storage_->UpdateContact(iter->second);
    } else {
      contacts_[peerId] = {
          .peer_id = peerId,
          .status = ContactStatus::awaitingApproval,
          .display_name = name,
          .bio = bio,
          .photo_base64 = photo
      };
      storage_->UpdateContact(contacts_[peerId]);
    }
    return RequestStatus::New;
  }

  // Confirms a contact that was awaiting our approval.
  auto ConfirmContact(ContactID const& peerId) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end() || iter->second.status != ContactStatus::awaitingApproval) {
      LGW << "Cannot confirm contact " << peerId.ToString() << "; not found or not awaiting approval.";
      return false;
    }
    iter->second.status = ContactStatus::confirmed;
    storage_->UpdateContact(iter->second);
    return true;
  }

  // Marks a contact as confirmed after they accepted our request.
  auto MarkAsConfirmed(ContactID const& peerId) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto iter = contacts_.find(peerId);

    if (iter != contacts_.end()) {
      if (iter->second.status == ContactStatus::confirmed) {
        return true;
      }
      iter->second.status = ContactStatus::confirmed;
      storage_->UpdateContact(iter->second);
    } else {
      contacts_[peerId] = {.peer_id = peerId, .status = ContactStatus::confirmed};
      storage_->UpdateContact(contacts_[peerId]);
    }
    return true;
  }

  void BlockContact(ContactID const& peerId) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto iter = contacts_.find(peerId);
    if (iter != contacts_.end()) {
      if (iter->second.status != ContactStatus::rejected) {
        iter->second.status = ContactStatus::rejected;
        storage_->UpdateContact(iter->second);
      }
    } else {
      contacts_[peerId] = {.peer_id = peerId, .status = ContactStatus::rejected};
      storage_->UpdateContact(contacts_[peerId]);
    }
  }

  void DeleteContact(ContactID const& peerId) {
    std::lock_guard<std::mutex> const lock(mutex_);
    contacts_.erase(peerId);
    storage_->RemoveContact(peerId);
  }

  [[nodiscard]] auto IsConfirmedContact(ContactID const& peerId) const -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    return (iter != contacts_.end() && iter->second.status == ContactStatus::confirmed);
  }

  [[nodiscard]] auto GetContact(ContactID const& peerId) -> std::optional<Contact> {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (contacts_.contains(peerId)) {
      return contacts_.at(peerId);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto GetContacts() const -> std::vector<Contact> {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::vector<Contact> result;
    result.reserve(contacts_.size());
    for (auto const& val : contacts_ | std::views::values) {
      result.push_back(val);
    }
    return result;
  }

  void UpdateContactProfile(
      ContactID const& peerId,
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  ) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end()) {
      LGW << "Cannot update profile for non-existent contact: " << peerId.ToString();
      return;
    }
    if (name) {
      iter->second.display_name = name;
    }
    if (bio) {
      iter->second.bio = bio;
    }
    if (photo) {
      iter->second.photo_base64 = photo;
    }
    storage_->UpdateContact(iter->second);
  }

  void SetDisplayNameOverride(ContactID const& peerId, std::string const& overrideName) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end()) {
      LGW << "Cannot set name override for non-existent contact: " << peerId.ToString();
      return;
    }
    constexpr size_t k_max_name_len = 64;
    std::string final_name = overrideName;
    if (final_name.length() > k_max_name_len) {
      final_name = final_name.substr(0, k_max_name_len);
    }
    iter->second.display_name_override = final_name;
    storage_->UpdateContact(iter->second);
  }

  void ResetDisplayNameOverride(ContactID const& peerId) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end()) {
      LGW << "Cannot reset name override for non-existent contact: " << peerId.ToString();
      return;
    }
    if (iter->second.display_name_override) {
      iter->second.display_name_override.reset();
      storage_->UpdateContact(iter->second);
    }
  }

  void StoreMySignatureFor(ContactID const& peerId, std::vector<std::byte> const& signature) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end()) {
      return;
    }
    iter->second.my_signature_for_them = signature;
    storage_->UpdateContact(iter->second);
  }

  void StoreSignatureFrom(ContactID const& peerId, std::vector<std::byte> const& signature) {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto const iter = contacts_.find(peerId);
    if (iter == contacts_.end()) {
      return;
    }
    iter->second.signature_from_them = signature;
    storage_->UpdateContact(iter->second);
  }

  auto GenerateContactToken() -> Token {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::erase_if(oneTimeTokens_, [now](auto const& item) {
      return (now - item.second.creationTime) > kTokenValidityDuration;
    });

    Token token;
    // NOLINTNEXTLINE(*-reinterpret-cast)
    RAND_bytes(reinterpret_cast<unsigned char*>(token.data()), static_cast<int>(token.size()));
    oneTimeTokens_[token] = {now};
    return token;
  }

  auto ValidateAndConsumeContactToken(std::vector<std::byte> const& tokenVec) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (tokenVec.size() != kTokenSize) {
      return false;
    }

    Token token;
    std::ranges::copy(tokenVec, token.begin());

    auto const itr = oneTimeTokens_.find(token);
    if (itr == oneTimeTokens_.end()) {
      return false;
    }
    auto const now = std::chrono::steady_clock::now();
    return now - itr->second.creationTime <= kTokenValidityDuration;
  }

  auto UnblockContact(ContactID const& peerId) -> bool {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto const itr = contacts_.find(peerId);
        itr != contacts_.end() && itr->second.status == ContactStatus::rejected) {
      itr->second.status = ContactStatus::awaitingApproval;
      storage_->UpdateContact(itr->second);
      return true;
    }
    return false;
  }

 private:
  void LoadContacts() {
    auto const contacts_from_db = storage_->GetContacts();
    for (auto const& contact : contacts_from_db) {
      contacts_[contact.peer_id] = contact;
    }
  }
};

}  // namespace eillance::identity
