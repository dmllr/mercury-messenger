#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/storage/persistent_storage.hpp"

namespace eillance {

struct IceConfigParams {
  std::string stun;
  std::string turn;
  std::string turn_user;
  std::string turn_pass;
};

class SettingsManager {
 public:
  explicit SettingsManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    Load();
  }

  // Retrieves the custom signaling server URL. Returns an empty optional if not set.
  [[nodiscard]] auto GetSignalingServerUrl() const -> std::optional<std::string> {
    return signalingServerUrl_;
  }

  // Sets and persists the custom signaling server URL.
  void SetSignalingServerUrl(std::string const& url) {
    signalingServerUrl_ = url;
    storage_->SetSetting(kSignalingServerUrlKey, url);
  }

  // --- User Profile ---
  [[nodiscard]] auto GetMyDisplayName() const -> std::optional<std::string> {
    return myDisplayName_;
  }
  [[nodiscard]] auto GetMyBio() const -> std::optional<std::string> {
    return myBio_;
  }
  [[nodiscard]] auto GetMyPhotoBase64() const -> std::optional<std::string> {
    return myPhotoBase64_;
  }

  void SetMyDisplayName(std::string const& name) {
    constexpr size_t max_name_length = 64;
    if (name.length() > max_name_length) {
      myDisplayName_ = name.substr(0, max_name_length);
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

  // --- Auditory Feedback ---
  [[nodiscard]] auto GetPlaySoundOnSend() const -> bool {
    auto val = storage_->GetSetting(kPlaySoundOnSendKey);
    return !val || (*val == "1");
  }

  void SetPlaySoundOnSend(bool enabled) {
    storage_->SetSetting(kPlaySoundOnSendKey, enabled ? "1" : "0");
  }

  [[nodiscard]] auto GetPlaySoundOnReceive() const -> bool {
    auto val = storage_->GetSetting(kPlaySoundOnRecvKey);
    return !val || (*val == "1");
  }

  void SetPlaySoundOnReceive(bool enabled) {
    storage_->SetSetting(kPlaySoundOnRecvKey, enabled ? "1" : "0");
  }

  // --- Notifications ---
  [[nodiscard]] auto GetShowNotificationContent() const -> bool {
    auto val = storage_->GetSetting(kShowNotificationContentKey);
    return !val || (*val == "1");
  }

  void SetShowNotificationContent(bool enabled) {
    storage_->SetSetting(kShowNotificationContentKey, enabled ? "1" : "0");
  }

  // --- Peer Connection ---
  [[nodiscard]] auto GetPeerInactivityTimeout() -> std::chrono::seconds {
    if (auto str_val = storage_->GetSetting(kPeerInactivityTimeoutKey)) {
      // TODO(unknown): check for user input correctness
      return std::chrono::seconds(std::stoi(*str_val));
    }
    constexpr auto default_timeout = 120U;
    return std::chrono::seconds(default_timeout);
  }

  void SetPeerInactivityTimeout(std::chrono::seconds timeout) {
    storage_->SetSetting(kPeerInactivityTimeoutKey, std::to_string(timeout.count()));
  }

  // --- Custom ICE Servers ---
  [[nodiscard]] auto GetCustomStun() const -> std::optional<std::string> {
    return customStun_;
  }
  [[nodiscard]] auto GetCustomTurn() const -> std::optional<std::string> {
    return customTurn_;
  }
  [[nodiscard]] auto GetCustomTurnUser() const -> std::optional<std::string> {
    return customTurnUser_;
  }
  [[nodiscard]] auto GetCustomTurnPass() const -> std::optional<std::string> {
    return customTurnPass_;
  }

  void SetCustomIceConfig(IceConfigParams const& params) {
    customStun_ = params.stun;
    customTurn_ = params.turn;
    customTurnUser_ = params.turn_user;
    customTurnPass_ = params.turn_pass;

    storage_->SetSetting(kCustomStunKey, params.stun);
    storage_->SetSetting(kCustomTurnKey, params.turn);
    storage_->SetSetting(kCustomTurnUserKey, params.turn_user);
  }

 private:
  void Load() {
    signalingServerUrl_ = storage_->GetSetting(kSignalingServerUrlKey);
    myDisplayName_ = storage_->GetSetting(kMyDisplayNameKey);
    myBio_ = storage_->GetSetting(kMyBioKey);
    myPhotoBase64_ = storage_->GetSetting(kMyPhotoBase64Key);
    customStun_ = storage_->GetSetting(kCustomStunKey);
    customTurn_ = storage_->GetSetting(kCustomTurnKey);
    customTurnUser_ = storage_->GetSetting(kCustomTurnUserKey);
    customTurnPass_ = storage_->GetSetting(kCustomTurnPassKey);
  }

  static constexpr auto kSignalingServerUrlKey = "signaling_server_url";
  static constexpr auto kMyDisplayNameKey = "my_display_name";
  static constexpr auto kMyBioKey = "my_bio";
  static constexpr auto kMyPhotoBase64Key = "my_photo_base64";
  static constexpr auto kPeerInactivityTimeoutKey = "peer_inactivity_timeout";
  static constexpr auto kCustomStunKey = "custom_ice_stun";
  static constexpr auto kCustomTurnKey = "custom_ice_turn";
  static constexpr auto kCustomTurnUserKey = "custom_ice_turn_user";
  static constexpr auto kCustomTurnPassKey = "custom_ice_turn_pass";
  static constexpr auto kPlaySoundOnSendKey = "sound_on_send";
  static constexpr auto kPlaySoundOnRecvKey = "sound_on_recv";
  static constexpr auto kShowNotificationContentKey = "show_notifications_content";

  std::shared_ptr<storage::PersistentStorage> storage_;

  // Cached settings
  std::optional<std::string> signalingServerUrl_;
  std::optional<std::string> myDisplayName_;
  std::optional<std::string> myBio_;
  std::optional<std::string> myPhotoBase64_;
  std::optional<std::string> customStun_;
  std::optional<std::string> customTurn_;
  std::optional<std::string> customTurnUser_;
  std::optional<std::string> customTurnPass_;
};

}  // namespace eillance
