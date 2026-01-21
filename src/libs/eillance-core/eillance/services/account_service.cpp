#include "eillance/services/account_service.hpp"

#include "eillance/logging/logging.hpp"

namespace eillance::services {

AccountService::AccountService(
    std::shared_ptr<storage::PersistentStorage> storage,
    std::shared_ptr<transport::Transport> transport
)
    : storage_(std::move(storage)),
      transport_(std::move(transport)) {
  if (auto db_path = storage_->GetPath(); !db_path.empty()) {
    backup_manager_ = std::make_unique<backup::BackupManager>(db_path);
  }

  id_manager_ = std::make_unique<identity::IdManager>(storage_);
  settings_manager_ = std::make_unique<SettingsManager>(storage_);
}

auto AccountService::GetId() const -> ContactID { return ContactID(id_manager_->GetPublicKey()); }

auto AccountService::IdentityExists() const -> bool { return id_manager_->IdentityExists(); }

auto AccountService::GetMnemonic() const -> std::optional<std::string> { return id_manager_->GetMnemonic(); }

auto AccountService::GenerateNewIdentity() -> std::string {
  auto mnemonic = id_manager_->GenerateNewIdentity();
  if (!mnemonic.empty()) {
    if (on_profile_updated_) {
      on_profile_updated_();
    }
  }
  return mnemonic;
}

auto AccountService::RestoreIdentity(std::string const& mnemonic) -> bool {
  if (id_manager_->RestoreFromMnemonic(mnemonic)) {
    if (on_profile_updated_) {
      on_profile_updated_();
    }
    return true;
  }
  return false;
}

auto AccountService::CreateBackup(std::string const& output_path, std::string const& password) const -> bool {
  if (!backup_manager_) {
    return false;
  }
  return backup_manager_->CreateBackup(output_path, password);
}

auto AccountService::RestoreBackup(std::string const& input_path, std::string const& password) -> bool {
  if (!backup_manager_) {
    return false;
  }
  if (backup_manager_->RestoreFromBackup(input_path, password)) {
    // Reload settings and identity after database restore
    id_manager_ = std::make_unique<identity::IdManager>(storage_);
    settings_manager_ = std::make_unique<SettingsManager>(storage_);
    if (on_profile_updated_) on_profile_updated_();
    return true;
  }
  return false;
}

auto AccountService::GetSettings() const -> SettingsManager* { return settings_manager_.get(); }

void AccountService::UpdateMyProfile(
    std::optional<std::string> const& name,
    std::optional<std::string> const& bio,
    std::optional<std::string> const& photo
) {
  if (name) {
    settings_manager_->SetMyDisplayName(*name);
  }
  if (bio) {
    settings_manager_->SetMyBio(*bio);
  }
  if (photo) {
    settings_manager_->SetMyPhotoBase64(*photo);
  }

  if (on_profile_updated_) on_profile_updated_();
}

void AccountService::UpdateSignalingUrl(std::string const& url) {
  settings_manager_->SetSignalingServerUrl(url);
  transport_->SetSignalingUrl(url);
  transport_->Reconnect();
}

auto AccountService::Sign(std::span<std::byte const> message) const -> std::vector<std::byte> {
  return id_manager_->Sign(message);
}

void AccountService::SetOnMyProfileUpdatedCallback(OnMyProfileUpdatedCallback callback) {
  on_profile_updated_ = std::move(callback);
}

}  // namespace eillance::services
