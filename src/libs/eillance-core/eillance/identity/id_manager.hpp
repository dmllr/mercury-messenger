#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "eillance/crypto/crypto.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

class IdManager {
  static constexpr auto identity_key_file = "identity.key";
  std::unique_ptr<crypto::Identity> identity_;
  std::shared_ptr<storage::PersistentStorage> storage_;

  void LoadOrGenerateIdentity() {
    const auto pem_data_opt = storage_->GetSetting(identity_key_file);
    if (pem_data_opt) {
      identity_ = crypto::Identity::LoadFromMemory(*pem_data_opt);
      if (identity_) {
        LOGI << "Identity loaded successfully. ID: " << identity_->GetId();
        return;
      }
      LOGE << "Failed to load identity from storage, will generate a new one.";
    }

    LOGI << "No existing identity found. Generating a new one.";
    identity_ = crypto::Identity::CreateNew();
    if (identity_) {
      const auto pem_str = identity_->SaveToMemory();
      storage_->SetSetting(identity_key_file, pem_str);
      LOGI << "New identity generated and saved. ID: " << identity_->GetId();
    }
  }

public:
  explicit IdManager(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) { LoadOrGenerateIdentity(); }

  auto GetIdentity() const -> crypto::Identity* { return identity_.get(); }

  auto GetId() const -> std::string { return identity_ ? identity_->GetId() : ""; }
};

}  // namespace eillance::identity
