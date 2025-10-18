#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "eillance/crypto/crypto.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

class IdManager {
  static constexpr auto IDENTITY_KEY_FILE = "identity.key";
  std::unique_ptr<crypto::Identity> identity_;
  std::shared_ptr<storage::PersistentStorage> storage_;

  void LoadOrGenerateIdentity() {
    if (storage_->Exists(IDENTITY_KEY_FILE)) {
      const auto pem_data_opt = storage_->Load(IDENTITY_KEY_FILE);
      if (pem_data_opt) {
        const std::string pem_str(reinterpret_cast<const char*>(pem_data_opt->data()), pem_data_opt->size());
        identity_ = crypto::Identity::LoadFromMemory(pem_str);
        if (identity_) {
          LOGI << "Identity loaded successfully. ID: " << identity_->GetId();
          return;
        }
        LOGE << "Failed to load identity from storage, will generate a new one.";
      }
    }

    LOGI << "No existing identity found. Generating a new one.";
    identity_ = crypto::Identity::CreateNew();
    if (identity_) {
      const auto pem_str = identity_->SaveToMemory();
      if (!pem_str.empty()) {
        const auto* char_ptr = reinterpret_cast<const std::byte*>(pem_str.data());
        const std::vector<std::byte> pem_bytes(char_ptr, char_ptr + pem_str.size());
        if (!storage_->Save(IDENTITY_KEY_FILE, pem_bytes)) {
          LOGE << "Failed to save new identity to persistent storage.";
        }
        LOGI << "New identity generated and saved. ID: " << identity_->GetId();
      }
    }
  }

public:
  explicit IdManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadOrGenerateIdentity();
  }

  crypto::Identity* GetIdentity() const {
    return identity_.get();
  }

  std::string GetId() const {
    return identity_ ? identity_->GetId() : "";
  }
};

}  // namespace eillance::identity
