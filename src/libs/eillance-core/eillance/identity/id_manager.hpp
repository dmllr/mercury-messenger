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
  static constexpr auto mnemonic_key_file = "identity.mnemonic";
  std::unique_ptr<crypto::Identity> identity_{nullptr};
  std::shared_ptr<storage::PersistentStorage> storage_;

  void LoadIdentity() {
    const auto pem_data_opt = storage_->GetSetting(identity_key_file);
    if (pem_data_opt) {
      identity_ = crypto::Identity::LoadFromMemory(*pem_data_opt);
      if (identity_) {
        LOGI << "Identity loaded successfully. ID: " << identity_->GetId();
      }
    }
  }

public:
  explicit IdManager(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) { LoadIdentity(); }

  auto GetIdentity() const -> crypto::Identity* { return identity_.get(); }

  auto GetId() const -> std::string { return identity_ ? identity_->GetId() : ""; }

  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
      return storage_->GetSetting(mnemonic_key_file);
  }

  [[nodiscard]] auto IdentityExists() const -> bool { return identity_ != nullptr; }

  auto GenerateNewIdentity() -> std::string {
    std::string mnemonic;
    identity_ = crypto::Identity::CreateNew(mnemonic);
    if (identity_) {
        const auto pem_str = identity_->SaveToMemory();
        storage_->SetSetting(identity_key_file, pem_str);
        storage_->SetSetting(mnemonic_key_file, mnemonic);
        LOGI << "New identity generated and saved. ID: " << identity_->GetId();
    }
    return mnemonic;
  }

  auto RestoreFromMnemonic(const std::string& mnemonic) -> bool {
      identity_ = crypto::Identity::GenerateFromMnemonic(mnemonic);
      if (identity_) {
          const auto pem_str = identity_->SaveToMemory();
          storage_->SetSetting(identity_key_file, pem_str);
          LOGI << "Identity restored from mnemonic. ID: " << identity_->GetId();
          return true;
      }
      return false;
  }

  auto Sign(const std::string& message) const -> std::vector<std::byte> {
    if (!identity_) { return {}; }
    return identity_->Sign(message);
  }
};

}  // namespace eillance::identity
