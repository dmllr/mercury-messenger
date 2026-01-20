#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/storage/persistent_storage.hpp"

namespace eillance {

class SettingsManager {
 public:
  explicit SettingsManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    Load();
  }

  // Retrieves the custom signaling server URL. Returns an empty optional if not set.
  [[nodiscard]] auto GetSignalingServerUrl() const -> std::optional<std::string> { return signalingServerUrl_; }

  // Sets and persists the custom signaling server URL.
  void SetSignalingServerUrl(std::string const& url) {
    signalingServerUrl_ = url;
    storage_->SetSetting(kSignalingServerUrlKey, url);
  }

  // --- User Profile ---
  [[nodiscard]] auto GetMyDisplayName() const -> std::optional<std::string> { return myDisplayName_; }
  [[nodiscard]] auto GetMyBio() const -> std::optional<std::string> { return myBio_; }
  [[nodiscard]] auto GetMyPhotoBase64() const -> std::optional<std::string> { return myPhotoBase64_; }

  void SetMyDisplayName(std::string const& name) {
    constexpr size_t k_max_name_length = 64;
    if (name.length() > k_max_name_length) {
      myDisplayName_ = name.substr(0, k_max_name_length);
    } else {
      myDisplayName_ = name;
    }
    storage_->SetSetting(kMyDisplayNameKey, *myDisplayName_);
  }

  void SetMyBio(std::string const& bio) {
    constexpr size_t max_bio_length = 300;
    if (bio.length() > max_bio_length) {
      myBio_ = bio.substr(0, max_bio_length);
    } else {
      myBio_ = bio;
    }
    storage_->SetSetting(kMyBioKey, *myBio_);
  }

  void SetMyPhotoBase64(std::string const& photo) {
    myPhotoBase64_ = photo;
    storage_->SetSetting(kMyPhotoBase64Key, photo);
  }

  // --- Peer Connection ---
  [[nodiscard]] auto GetPeerInactivityTimeout() const -> std::chrono::seconds {
    if (auto str_val = storage_->GetSetting(kPeerInactivityTimeoutKey)) {
      // TODO: check for user input correctness
      return std::chrono::seconds(std::stoi(*str_val));
    }
    constexpr auto default_timeout = 120U;
    return std::chrono::seconds(default_timeout);
  }

  void SetPeerInactivityTimeout(std::chrono::seconds timeout) {
    storage_->SetSetting(kPeerInactivityTimeoutKey, std::to_string(timeout.count()));
  }

 private:
  void Load() {
    signalingServerUrl_ = storage_->GetSetting(kSignalingServerUrlKey);
    myDisplayName_ = storage_->GetSetting(kMyDisplayNameKey);
    myBio_ = storage_->GetSetting(kMyBioKey);
    myPhotoBase64_ = storage_->GetSetting(kMyPhotoBase64Key);
  }

  static constexpr auto kSignalingServerUrlKey = "signaling_server_url";
  static constexpr auto kMyDisplayNameKey = "my_display_name";
  static constexpr auto kMyBioKey = "my_bio";
  static constexpr auto kMyPhotoBase64Key = "my_photo_base64";
  static constexpr auto kPeerInactivityTimeoutKey = "peer_inactivity_timeout";

  std::shared_ptr<storage::PersistentStorage> storage_;

  // Cached settings
  std::optional<std::string> signalingServerUrl_;
  std::optional<std::string> myDisplayName_;
  std::optional<std::string> myBio_;
  std::optional<std::string> myPhotoBase64_;
};

}  // namespace eillance
