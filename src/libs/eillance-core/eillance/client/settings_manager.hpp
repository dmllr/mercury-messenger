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
  explicit SettingsManager(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) { Load(); }

  // Retrieves the custom signaling server URL. Returns an empty optional if not set.
  [[nodiscard]] auto GetSignalingServerUrl() const -> std::optional<std::string> { return signaling_server_url_; }

  // Sets and persists the custom signaling server URL.
  void SetSignalingServerUrl(const std::string& url) {
    signaling_server_url_ = url;
    storage_->SetSetting(kSignalingServerUrlKey, url);
  }

  // --- User Profile ---
  [[nodiscard]] auto GetMyDisplayName() const -> std::optional<std::string> { return my_display_name_; }
  [[nodiscard]] auto GetMyBio() const -> std::optional<std::string> { return my_bio_; }
  [[nodiscard]] auto GetMyPhotoBase64() const -> std::optional<std::string> { return my_photo_base64_; }

  void SetMyDisplayName(const std::string& name) {
    if (name.length() > 64) {
      my_display_name_ = name.substr(0, 64);
    } else {
      my_display_name_ = name;
    }
    storage_->SetSetting(kMyDisplayNameKey, *my_display_name_);
  }

  void SetMyBio(const std::string& bio) {
    if (bio.length() > 300) {
      my_bio_ = bio.substr(0, 300);
    } else {
      my_bio_ = bio;
    }
    storage_->SetSetting(kMyBioKey, *my_bio_);
  }

  void SetMyPhotoBase64(const std::string& photo) {
    my_photo_base64_ = photo;
    storage_->SetSetting(kMyPhotoBase64Key, photo);
  }

  // --- Peer Connection ---
  [[nodiscard]] auto GetPeerInactivityTimeout() const -> std::chrono::seconds {
    if (auto str_val = storage_->GetSetting(kPeerInactivityTimeoutKey)) {
      try {
        return std::chrono::seconds(std::stoi(*str_val));
      } catch (...) {}
    }
    return std::chrono::seconds(120);
  }

  void SetPeerInactivityTimeout(std::chrono::seconds timeout) {
    storage_->SetSetting(kPeerInactivityTimeoutKey, std::to_string(timeout.count()));
  }

private:
  void Load() {
    signaling_server_url_ = storage_->GetSetting(kSignalingServerUrlKey);
    my_display_name_ = storage_->GetSetting(kMyDisplayNameKey);
    my_bio_ = storage_->GetSetting(kMyBioKey);
    my_photo_base64_ = storage_->GetSetting(kMyPhotoBase64Key);
  }

  static constexpr auto kSignalingServerUrlKey = "signaling_server_url";
  static constexpr auto kMyDisplayNameKey = "my_display_name";
  static constexpr auto kMyBioKey = "my_bio";
  static constexpr auto kMyPhotoBase64Key = "my_photo_base64";
  static constexpr auto kPeerInactivityTimeoutKey = "peer_inactivity_timeout";

  std::shared_ptr<storage::PersistentStorage> storage_;

  // Cached settings
  std::optional<std::string> signaling_server_url_;
  std::optional<std::string> my_display_name_;
  std::optional<std::string> my_bio_;
  std::optional<std::string> my_photo_base64_;
};

}  // namespace eillance
