#include "profile_view_model.hpp"

namespace eillance::view_model {

ProfileViewModel::ProfileViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)), router_(std::move(router)) {
  auto& queue = router_->Queue();
  Subscribe<EventMyProfileUpdated>(queue, [this](const auto&) { RefreshFromSettings(); });
  RefreshFromSettings();
}

ProfileViewModel::~ProfileViewModel() {
  for (const auto& unsubscribe: eventUnsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void ProfileViewModel::Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ProfileViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  const std::lock_guard<std::mutex> lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnProfileUpdated(profile_);
  }
}

auto ProfileViewModel::GetProfile() const -> MyProfile {
  const std::lock_guard<std::mutex> lock(mutex_);
  return profile_;
}

void ProfileViewModel::UpdateProfile(const std::optional<std::string>& name,
                                     const std::optional<std::string>& bio,
                                     const std::optional<std::string>& photo) {
  client_->UpdateMyProfile(name, bio, photo);
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
  const std::lock_guard<std::mutex> lock(mutex_);
  bool changed = false;
  const bool exists = client_->IdentityExists();
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

  if (changed) {
    if (observer_) {
      observer_->OnProfileUpdated(profile_);
    }
  }
}

}  // namespace eillance::view_model
