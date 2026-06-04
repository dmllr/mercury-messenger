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
  static constexpr auto identityKeyFile = "identity.key";
  static constexpr auto mnemonicKeyFile = "identity.mnemonic";
  static constexpr auto deviceKeyFile = "device.key";

  std::unique_ptr<crypto::Identity> identity_{nullptr};
  std::unique_ptr<crypto::Identity> device_identity_{nullptr};
  std::shared_ptr<storage::PersistentStorage> storage_;

  void LoadIdentity() {
    if (auto const pem_data_opt = storage_->GetSetting(identityKeyFile)) {
      identity_ = crypto::Identity::LoadFromMemory(*pem_data_opt);
      if (identity_) {
        LGI << "Identity loaded successfully.";
      }
    }
    if (auto const dev_data = storage_->GetSetting(deviceKeyFile)) {
      device_identity_ = crypto::Identity::LoadFromMemory(*dev_data);
    }
    if (!device_identity_) {
      std::string dummy_mnemonic;
      device_identity_ = crypto::Identity::CreateNew(dummy_mnemonic);
      if (device_identity_) {
        storage_->SetSetting(deviceKeyFile, device_identity_->SaveToMemory());
        LGI << "New device network identity generated and saved.";
      }
    }
  }

 public:
  explicit IdManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    LoadIdentity();
  }

  [[nodiscard]] auto GetIdentity() const -> crypto::Identity* {
    return identity_.get();
  }

  [[nodiscard]] auto GetPublicKey() const -> std::vector<std::byte> {
    return identity_ ? identity_->GetPublicKey() : std::vector<std::byte>{};
  }

  [[nodiscard]] auto GetDevicePublicKey() const -> std::vector<std::byte> {
    return device_identity_ ? device_identity_->GetPublicKey() : std::vector<std::byte>{};
  }

  [[nodiscard]] auto GetDevicePrivateKey() const -> std::vector<std::byte> {
    return device_identity_ ? device_identity_->GetPrivateKey() : std::vector<std::byte>{};
  }

  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
    return storage_->GetSetting(mnemonicKeyFile);
  }

  [[nodiscard]] auto IdentityExists() const -> bool {
    return identity_ != nullptr;
  }

  auto GenerateNewIdentity() -> std::string {
    std::string mnemonic;
    identity_ = crypto::Identity::CreateNew(mnemonic);
    if (identity_) {
      auto const pem_str = identity_->SaveToMemory();
      storage_->SetSetting(identityKeyFile, pem_str);
      storage_->SetSetting(mnemonicKeyFile, mnemonic);
      LGI << "New identity generated and saved.";
    }
    return mnemonic;
  }

  auto RestoreFromMnemonic(std::string const& mnemonic) -> bool {
    identity_ = crypto::Identity::GenerateFromMnemonic(mnemonic);
    if (identity_) {
      auto const pem_str = identity_->SaveToMemory();
      storage_->SetSetting(identityKeyFile, pem_str);
      storage_->SetSetting(mnemonicKeyFile, mnemonic);
      LGI << "Identity restored from mnemonic.";
      return true;
    }
    return false;
  }

  [[nodiscard]] auto Sign(std::span<std::byte const> const message) const -> std::vector<std::byte> {
    if (!identity_) {
      return {};
    }
    return identity_->Sign(message);
  }

  [[nodiscard]] auto SignWithDevice(std::span<std::byte const> const message) const -> std::vector<std::byte> {
    if (!device_identity_) {
      return {};
    }
    return device_identity_->Sign(message);
  }
};

}  // namespace eillance::identity
