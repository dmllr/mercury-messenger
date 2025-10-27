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
    Save();
  }

private:
  void Load() {
    if (auto data = storage_->Load(kSignalingServerUrlKey)) {
      signaling_server_url_ = std::string(reinterpret_cast<const char*>(data->data()), data->size());
    }
  }

  void Save() const {
    if (signaling_server_url_) {
      const auto& url = *signaling_server_url_;
      const std::vector<std::byte> data(reinterpret_cast<const std::byte*>(url.data()), reinterpret_cast<const std::byte*>(url.data()) + url.size());
      storage_->Save(kSignalingServerUrlKey, data);
    } else {
      storage_->Delete(kSignalingServerUrlKey);
    }
  }

  static constexpr auto kSignalingServerUrlKey = "signaling_server_url";
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::optional<std::string> signaling_server_url_;
};

}  // namespace eillance
