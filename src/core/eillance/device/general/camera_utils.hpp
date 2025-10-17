#pragma once

#include <cinttypes>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>


namespace eillance {

inline auto GetAvailableCameras() -> std::vector<std::pair<std::string, std::string>> {
  return {};
}

inline auto GetSupportedResolutions(const std::string& cameraId) -> std::vector<std::pair<int32_t, int32_t>> {
  return {};
}

inline auto GetDeviceProfileAsJson() -> nlohmann::json {
  return {};
}

}  // namespace eillance
