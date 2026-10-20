#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/network_config.hpp"

namespace eillance {

struct IceConfigParams {
  std::string turn;
  std::string turn_user;
  std::string turn_pass;
  bool use_custom_turn_only = false;
};

class SettingsManager {
 public:
  explicit SettingsManager(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
    Load();
  }

  [[nodiscard]] static auto GetDefaultBootstrapNodeUrls() -> std::string {
    return transport::GetDefaultBootstrapNodeUrls();
  }

  [[nodiscard]] static auto ParseUrls(std::string const& input) -> std::vector<std::string> {
    return transport::ParseUrls(input);
  }

  [[nodiscard]] auto GetBootstrapNodeUrls() const -> std::optional<std::string> {
    return bootstrapNodeUrls_;
  }

  [[nodiscard]] auto GetAppendBootstrapNodes() const -> bool {
    auto const val = storage_->GetSetting(keyAppendBootstrapNodes);
    return !val || (*val == "1");
  }

  [[nodiscard]] auto GetEffectiveBootstrapNodeUrls() const -> std::vector<std::string> {
    auto&& default_urls = ParseUrls(GetDefaultBootstrapNodeUrls());
    if (!bootstrapNodeUrls_ || bootstrapNodeUrls_->empty()) {
      return default_urls;
    }
    auto&& custom_urls = ParseUrls(*bootstrapNodeUrls_);
    if (custom_urls.empty()) {
      return default_urls;
    }
    if (!GetAppendBootstrapNodes()) {
      return custom_urls;
    }
    std::vector<std::string> combined = default_urls;
    for (auto const& url : custom_urls) {
      if (std::ranges::find(combined, url) == combined.end()) {
        combined.push_back(url);
      }
    }
    return combined;
  }

  void SetBootstrapNodeUrls(std::string const& urls, bool append_to_default) {
    appendBootstrapNodes_ = append_to_default;
    storage_->SetSetting(keyAppendBootstrapNodes, append_to_default ? "1" : "0");
    bootstrapNodeUrls_ = urls;
    storage_->SetSetting(keyBootstrapNodeUrls, urls);
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
    storage_->SetSetting(keyMyDisplayName, *myDisplayName_);
  }

  void SetMyBio(std::string const& bio) {
    constexpr size_t max_bio_length = 300;
    if (bio.length() > max_bio_length) {
      myBio_ = bio.substr(0, max_bio_length);
    } else {
      myBio_ = bio;
    }
    storage_->SetSetting(keyMyBio, *myBio_);
  }

  void SetMyPhotoBase64(std::string const& photo) {
    myPhotoBase64_ = photo;
    storage_->SetSetting(keyMyPhotoBase64, photo);
  }

  // --- Auditory Feedback ---
  [[nodiscard]] auto GetPlaySoundOnSend() const -> bool {
    auto const val = storage_->GetSetting(keyPlaySoundOnSend);
    return !val || (*val == "1");
  }

  void SetPlaySoundOnSend(bool enabled) {
    storage_->SetSetting(keyPlaySoundOnSend, enabled ? "1" : "0");
  }

  [[nodiscard]] auto GetPlaySoundOnReceive() const -> bool {
    auto const val = storage_->GetSetting(keyPlaySoundOnRecv);
    return !val || (*val == "1");
  }

  void SetPlaySoundOnReceive(bool enabled) {
    storage_->SetSetting(keyPlaySoundOnRecv, enabled ? "1" : "0");
  }

  // --- Notifications ---
  [[nodiscard]] auto GetShowNotificationContent() const -> bool {
    auto const val = storage_->GetSetting(keyShowNotificationContent);
    return !val || (*val == "1");
  }

  void SetShowNotificationContent(bool enabled) {
    storage_->SetSetting(keyShowNotificationContent, enabled ? "1" : "0");
  }

  // --- Peer Connection ---
  [[nodiscard]] auto GetPeerInactivityTimeout() -> std::chrono::seconds {
    if (auto const str_val = storage_->GetSetting(keyPeerInactivityTimeout)) {
      // TODO(unknown): check for user input correctness
      return std::chrono::seconds(std::stoi(*str_val));
    }
    constexpr auto default_timeout = 120U;
    return std::chrono::seconds(default_timeout);
  }

  void SetPeerInactivityTimeout(std::chrono::seconds timeout) {
    storage_->SetSetting(keyPeerInactivityTimeout, std::to_string(timeout.count()));
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
  [[nodiscard]] auto GetUseCustomTurnOnly() const -> bool {
    return useCustomTurnOnly_;
  }

  void SetCustomIceConfig(IceConfigParams const& params) {
    customTurn_ = params.turn;
    customTurnUser_ = params.turn_user;
    customTurnPass_ = params.turn_pass;
    useCustomTurnOnly_ = params.use_custom_turn_only;

    storage_->SetSetting(keyCustomTurn, params.turn);
    storage_->SetSetting(keyCustomTurnUser, params.turn_user);
  }

  [[nodiscard]] auto GetIgnoreBatteryOptimization() const -> bool {
    auto const val = storage_->GetSetting(keyIgnoreBattery);
    return val && (*val == "1");
  }

  void SetIgnoreBatteryOptimization(bool ignore) {
    storage_->SetSetting(keyIgnoreBattery, ignore ? "1" : "0");
  }

  [[nodiscard]] auto GetIgnoreCallPermissions() const -> bool {
    auto const val = storage_->GetSetting(keyIgnoreCalls);
    return val && (*val == "1");
  }

  void SetIgnoreCallPermissions(bool ignore) {
    storage_->SetSetting(keyIgnoreCalls, ignore ? "1" : "0");
  }

 private:
  void Load() {
    bootstrapNodeUrls_ = storage_->GetSetting(keyBootstrapNodeUrls);
    if (!bootstrapNodeUrls_) {
      if (auto const legacy = storage_->GetSetting("bootstrap_node_url")) {
        if (*legacy != "/dns4/app.hela.me/tcp/10807") {
          bootstrapNodeUrls_ = *legacy;
          storage_->SetSetting(keyBootstrapNodeUrls, *bootstrapNodeUrls_);
        }
        storage_->DeleteSetting("bootstrap_node_url");
      }
    }
    appendBootstrapNodes_ = GetAppendBootstrapNodes();

    myDisplayName_ = storage_->GetSetting(keyMyDisplayName);
    myBio_ = storage_->GetSetting(keyMyBio);
    myPhotoBase64_ = storage_->GetSetting(keyMyPhotoBase64);
    customTurn_ = storage_->GetSetting(keyCustomTurn);
    customTurnUser_ = storage_->GetSetting(keyCustomTurnUser);
    customTurnPass_ = storage_->GetSetting(keyCustomTurnPass);
    auto const turn_only_val = storage_->GetSetting(keyUseCustomTurnOnly);
    useCustomTurnOnly_ = turn_only_val && (*turn_only_val == "1");
  }

  static constexpr auto keyBootstrapNodeUrls = "bootstrap_node_urls";
  static constexpr auto keyAppendBootstrapNodes = "append_bootstrap_nodes";
  static constexpr auto keyMyDisplayName = "my_display_name";
  static constexpr auto keyMyBio = "my_bio";
  static constexpr auto keyMyPhotoBase64 = "my_photo_base64";
  static constexpr auto keyPeerInactivityTimeout = "peer_inactivity_timeout";
  static constexpr auto keyCustomTurn = "custom_ice_turn";
  static constexpr auto keyCustomTurnUser = "custom_ice_turn_user";
  static constexpr auto keyCustomTurnPass = "custom_ice_turn_pass";
  static constexpr auto keyUseCustomTurnOnly = "use_custom_turn_only";
  static constexpr auto keyPlaySoundOnSend = "sound_on_send";
  static constexpr auto keyPlaySoundOnRecv = "sound_on_recv";
  static constexpr auto keyShowNotificationContent = "show_notifications_content";
  static constexpr auto keyIgnoreBattery = "ignore_battery_opt";
  static constexpr auto keyIgnoreCalls = "ignore_call_perms";

  std::shared_ptr<storage::PersistentStorage> storage_;

  // Cached settings
  std::optional<std::string> bootstrapNodeUrls_;
  bool appendBootstrapNodes_ = true;
  std::optional<std::string> myDisplayName_;
  std::optional<std::string> myBio_;
  std::optional<std::string> myPhotoBase64_;
  std::optional<std::string> customTurn_;
  std::optional<std::string> customTurnUser_;
  std::optional<std::string> customTurnPass_;
  bool useCustomTurnOnly_ = false;
};

}  // namespace eillance
