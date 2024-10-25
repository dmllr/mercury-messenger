#pragma once

#include <cinttypes>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>


namespace hela::eillance {

auto GetAvailableCameras() -> std::vector<std::pair<std::string, std::string>> {
  return {};
}

auto GetSupportedResolutions(const std::string& cameraId) -> std::vector<std::pair<int32_t, int32_t>> {
  return {};
}

auto GetDeviceProfileAsJson() -> nlohmann::json {
  return {};
}

}  // namespace hela::eillance
