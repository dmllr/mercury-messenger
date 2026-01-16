#include "profile_view_model.hpp"

namespace eillance::view_model {

ProfileViewModel::ProfileViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier)
    : client_(std::move(client)), notifier_(std::move(notifier)) {
  RefreshFromSettings();
}

auto ProfileViewModel::GetProfile() const -> MyProfile {
  const std::lock_guard<std::mutex> lock(mutex_);
  return profile_;
}

void ProfileViewModel::UpdateProfile(const std::optional<std::string>& name,
                                     const std::optional<std::string>& bio,
                                     const std::optional<std::string>& photo) {
  client_->UpdateMyProfile(name, bio, photo);
  // Client callback will trigger RefreshFromSettings
}

auto ProfileViewModel::GenerateNewIdentity() -> std::string {
  auto mnemonic = client_->GenerateNewIdentity();
  if (!mnemonic.empty()) {
    RefreshFromSettings();
  }
  return mnemonic;
}

auto ProfileViewModel::RestoreIdentity(const std::string& mnemonic) -> bool {
  if (client_->RestoreIdentity(mnemonic)) {
    RefreshFromSettings();
    return true;
  }
  return false;
}

auto ProfileViewModel::GetMnemonic() -> std::optional<std::string> {
  return client_->GetMnemonic();
}

auto ProfileViewModel::CreateBackup(const std::string& path, const std::string& password) -> bool {
  return client_->CreateBackup(path, password);
}

auto ProfileViewModel::RestoreBackup(const std::string& path, const std::string& password) -> bool {
  if (client_->RestoreBackup(path, password)) {
    RefreshFromSettings();
    return true;
  }
  return false;
}

void ProfileViewModel::RefreshFromSettings() {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    bool exists = client_->IdentityExists();
    if (profile_.identity_exists != exists) {
      profile_.identity_exists = exists;
      changed = true;
    }

    if (exists) {
      auto id = client_->GetId();
      if (profile_.my_id != id) {
        profile_.my_id = id;
        changed = true;
      }

      const auto* settings = client_->GetSettingsManager();
      auto name = settings->GetMyDisplayName();
      auto bio = settings->GetMyBio();
      auto photo = settings->GetMyPhotoBase64();

      if (profile_.my_display_name != name || profile_.my_bio != bio || profile_.my_photo != photo) {
        profile_.my_display_name = name;
        profile_.my_bio = bio;
        profile_.my_photo = photo;
        changed = true;
      }
    }
  }

  if (changed && notifier_) {
    notifier_(StateChange::MyProfile);
  }
}

}  // namespace eillance::view_model
