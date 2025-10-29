#pragma once

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

private:
  void Load() {
    signaling_server_url_ = storage_->GetSetting(kSignalingServerUrlKey);
  }

  static constexpr auto kSignalingServerUrlKey = "signaling_server_url";
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::optional<std::string> signaling_server_url_;
};

}  // namespace eillance
