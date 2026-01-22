#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::identity {

class IdManager {
  static constexpr auto kIdentityKeyFile = "identity.key";
  static constexpr auto kMnemonicKeyFile = "identity.mnemonic";
  std::unique_ptr<crypto::Identity> identity_{nullptr};
  std::shared_ptr<storage::PersistentStorage> storage_;

  void LoadIdentity() {
    auto const pem_data_opt = storage_->GetSetting(kIdentityKeyFile);
    if (pem_data_opt) {
      identity_ = crypto::Identity::LoadFromMemory(*pem_data_opt);
      if (identity_) {
        LGI << "Identity loaded successfully.";
      }
    }
  }

 public:
  explicit IdManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadIdentity();
  }

  [[nodiscard]] auto GetIdentity() const -> crypto::Identity* { return identity_.get(); }

  [[nodiscard]] auto GetPublicKey() const -> std::vector<std::byte> {
    return identity_ ? identity_->GetPublicKey() : std::vector<std::byte>{};
  }

  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
    return storage_->GetSetting(kMnemonicKeyFile);
  }

  [[nodiscard]] auto IdentityExists() const -> bool { return identity_ != nullptr; }

  auto GenerateNewIdentity() -> std::string {
    std::string mnemonic;
    identity_ = crypto::Identity::CreateNew(mnemonic);
    if (identity_) {
      auto const pem_str = identity_->SaveToMemory();
      storage_->SetSetting(kIdentityKeyFile, pem_str);
      storage_->SetSetting(kMnemonicKeyFile, mnemonic);
      LGI << "New identity generated and saved.";
    }
    return mnemonic;
  }

  auto RestoreFromMnemonic(std::string const& mnemonic) -> bool {
    identity_ = crypto::Identity::GenerateFromMnemonic(mnemonic);
    if (identity_) {
      auto const pem_str = identity_->SaveToMemory();
      storage_->SetSetting(kIdentityKeyFile, pem_str);
      storage_->SetSetting(kMnemonicKeyFile, mnemonic);
      LGI << "Identity restored from mnemonic.";
      return true;
    }
    return false;
  }

  [[nodiscard]] auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> {
    if (!identity_) {
      return {};
    }
    return identity_->Sign(message);
  }
};

}  // namespace eillance::identity
