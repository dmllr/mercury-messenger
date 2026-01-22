#include "profile_view_model.hpp"

#include <mutex>

namespace eillance::view_model {

struct ProfileViewModel::Impl : public std::enable_shared_from_this<Impl> {
  std::shared_ptr<Client> client;
  std::shared_ptr<ClientEventRouter> router;
  std::shared_ptr<Observer> observer;

  mutable std::mutex mutex;
  MyProfile profile;

  Impl(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : client(std::move(client)),
        router(std::move(router)) {}

  void SetObserver(std::shared_ptr<Observer> obs) {
    std::lock_guard<std::mutex> const lock(mutex);
    observer = std::move(obs);
    if (observer) {
      observer->OnProfileUpdated(profile);
    }
  }

  auto GetProfile() const -> MyProfile {
    std::lock_guard<std::mutex> const lock(mutex);
    return profile;
  }

  void UpdateProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  ) {
    client->UpdateMyProfile(name, bio, photo);
  }

  auto GenerateNewIdentity() -> std::string {
    auto mnemonic = client->GenerateNewIdentity();
    if (!mnemonic.empty()) RefreshFromSettings();
    return mnemonic;
  }

  auto RestoreIdentity(std::string const& mnemonic) -> bool {
    if (client->RestoreIdentity(mnemonic)) {
      RefreshFromSettings();
      return true;
    }
    return false;
  }

  auto RestoreBackup(std::string const& path, std::string const& password) -> bool {
    if (client->RestoreBackup(path, password)) {
      RefreshFromSettings();
      return true;
    }
    return false;
  }

  void RefreshFromSettings() {
    std::lock_guard<std::mutex> const lock(mutex);
    bool changed = false;
    bool const exists = client->IdentityExists();
    if (profile.identity_exists != exists) {
      profile.identity_exists = exists;
      changed = true;
    }

    if (exists) {
      auto id = client->GetId();
      if (profile.my_id != id) {
        profile.my_id = id;
        changed = true;
      }

      auto const* settings = client->GetSettingsManager();
      auto name = settings->GetMyDisplayName();
      auto bio = settings->GetMyBio();
      auto photo = settings->GetMyPhotoBase64();

      if (profile.my_display_name != name || profile.my_bio != bio || profile.my_photo != photo) {
        profile.my_display_name = name;
        profile.my_bio = bio;
        profile.my_photo = photo;
        changed = true;
      }
    }

    if (changed && observer) {
      observer->OnProfileUpdated(profile);
    }
  }
};

ProfileViewModel::ProfileViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : impl_(std::make_shared<Impl>(std::move(client), std::move(router))) {
  auto& queue = impl_->router->Queue();
  auto impl = impl_;
  Subscribe<EventMyProfileUpdated>(queue, [impl](auto const&) { impl->RefreshFromSettings(); });
  impl_->RefreshFromSettings();
}

ProfileViewModel::~ProfileViewModel() {
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
}

template <typename EventType>
void ProfileViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ProfileViewModel::SetObserver(std::shared_ptr<Observer> observer) const {
  impl_->SetObserver(std::move(observer));
}
auto ProfileViewModel::GetProfile() const -> MyProfile { return impl_->GetProfile(); }
void ProfileViewModel::UpdateProfile(
    std::optional<std::string> const& name,
    std::optional<std::string> const& bio,
    std::optional<std::string> const& photo
) {
  impl_->UpdateProfile(name, bio, photo);
}
auto ProfileViewModel::GenerateNewIdentity() -> std::string { return impl_->GenerateNewIdentity(); }
auto ProfileViewModel::RestoreIdentity(std::string const& mnemonic) -> bool { return impl_->RestoreIdentity(mnemonic); }
auto ProfileViewModel::GetMnemonic() -> std::optional<std::string> { return impl_->client->GetMnemonic(); }
auto ProfileViewModel::CreateBackup(std::string const& path, std::string const& password) -> bool {
  return impl_->client->CreateBackup(path, password);
}
auto ProfileViewModel::RestoreBackup(std::string const& path, std::string const& password) -> bool {
  return impl_->RestoreBackup(path, password);
}

}  // namespace eillance::view_model
