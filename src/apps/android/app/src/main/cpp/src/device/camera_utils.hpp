#pragma once

#include <array>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <cinttypes>
#include <media/NdkImage.h>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "eillance/logging/logging.hpp"


namespace eillance {

inline auto GetAvailableCameras() -> std::vector<std::pair<std::string, std::string>> {
  ACameraManager* camera_manager = ACameraManager_create();
  if (camera_manager == nullptr) {
    LGE << "Failed to create camera manager";
    return {};
  }

  ACameraIdList* camera_id_list = nullptr;
  if (ACameraManager_getCameraIdList(camera_manager, &camera_id_list) != ACAMERA_OK) {
    LGE << "Failed to get camera ID list";
    ACameraManager_delete(camera_manager);
    return {};
  }

  std::vector<std::pair<std::string, std::string>> cameras;
  for (int i = 0; i < camera_id_list->numCameras; ++i) {
    const char* camera_id = camera_id_list->cameraIds[i];

    ACameraMetadata* metadata = nullptr;
    if (ACameraManager_getCameraCharacteristics(camera_manager, camera_id, &metadata) != ACAMERA_OK) {
      LGE << "Failed to get camera characteristics for camera ID: " << camera_id;
      continue;
    }

    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK) {
      const char* facing = (entry.data.u8[0] == ACAMERA_LENS_FACING_FRONT) ? "Front" : "Back";
      cameras.emplace_back(camera_id, facing);
    }

    ACameraMetadata_free(metadata);
  }

  ACameraManager_deleteCameraIdList(camera_id_list);
  ACameraManager_delete(camera_manager);

  return cameras;
}

inline auto GetSupportedResolutions(const std::string& camera_id) -> std::vector<std::pair<int32_t, int32_t>> {
  ACameraManager* camera_manager = ACameraManager_create();
  if (camera_manager == nullptr) {
    LGE << "Failed to create camera manager";
    return {};
  }

  ACameraMetadata* metadata = nullptr;
  if (ACameraManager_getCameraCharacteristics(camera_manager, camera_id.c_str(), &metadata) != ACAMERA_OK) {
    LGE << "Failed to get camera characteristics for camera ID: " << camera_id.c_str();
    ACameraManager_delete(camera_manager);
    return {};
  }

  ACameraMetadata_const_entry entry;
  std::vector<std::pair<int32_t, int32_t>> resolutions;
  if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
    for (uint32_t i = 0; i < entry.count; i += 4) {
      if (entry.data.i32[i + 3] == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) { resolutions.emplace_back(entry.data.i32[i + 1], entry.data.i32[i + 2]); }
    }
  }

  ACameraMetadata_free(metadata);
  ACameraManager_delete(camera_manager);

  return resolutions;
}

inline auto GetDeviceProfileAsJson() -> nlohmann::json {
  using Json = nlohmann::json;
  auto cameras = GetAvailableCameras();
  auto cameras_json = Json::array();

  for (const auto& camera: cameras) {
    Json camera_json{};
    camera_json["id"] = camera.first;
    camera_json["name"] = camera.second;

    auto resolutions = GetSupportedResolutions(camera.first);
    // Erase the duplicate resolutions
    std::sort(resolutions.begin(), resolutions.end());
    auto last = std::unique(resolutions.begin(), resolutions.end());
    resolutions.erase(last, resolutions.end());

    Json resolutions_json = Json::array();
    for (const auto& res: resolutions) {
      resolutions_json.push_back({{"width", res.first}, {"height", res.second}});
    }

    camera_json["resolutions"] = resolutions_json;
    cameras_json.push_back(camera_json);
  }

  return cameras_json;
}

template<typename T, std::size_t N>
constexpr auto GetPairStr(T key, const std::array<std::pair<T, const char*>, N>& store) -> const char* {
  for (const auto& pair: store) {
    if (pair.first == key) { return pair.second; }
  }
  if (!std::is_constant_evaluated()) { Log::w("(%#08x) : UNKNOWN TAG for %s", key, typeid(store[0].first).name()); }
  return "UNKNOWN";
}

using ErrorPair = std::pair<camera_status_t, const char*>;
static constexpr std::array<ErrorPair, 15> camera_error_info{{
    {ACAMERA_OK, "ACAMERA_OK"},
    {ACAMERA_ERROR_UNKNOWN, "ACAMERA_ERROR_UNKNOWN"},
    {ACAMERA_ERROR_INVALID_PARAMETER, "ACAMERA_ERROR_INVALID_PARAMETER"},
    {ACAMERA_ERROR_CAMERA_DISCONNECTED, "ACAMERA_ERROR_CAMERA_DISCONNECTED"},
    {ACAMERA_ERROR_NOT_ENOUGH_MEMORY, "ACAMERA_ERROR_NOT_ENOUGH_MEMORY"},
    {ACAMERA_ERROR_METADATA_NOT_FOUND, "ACAMERA_ERROR_METADATA_NOT_FOUND"},
    {ACAMERA_ERROR_CAMERA_DEVICE, "ACAMERA_ERROR_CAMERA_DEVICE"},
    {ACAMERA_ERROR_CAMERA_SERVICE, "ACAMERA_ERROR_CAMERA_SERVICE"},
    {ACAMERA_ERROR_SESSION_CLOSED, "ACAMERA_ERROR_SESSION_CLOSED"},
    {ACAMERA_ERROR_INVALID_OPERATION, "ACAMERA_ERROR_INVALID_OPERATION"},
    {ACAMERA_ERROR_STREAM_CONFIGURE_FAIL, "ACAMERA_ERROR_STREAM_CONFIGURE_FAIL"},
    {ACAMERA_ERROR_CAMERA_IN_USE, "ACAMERA_ERROR_CAMERA_IN_USE"},
    {ACAMERA_ERROR_MAX_CAMERA_IN_USE, "ACAMERA_ERROR_MAX_CAMERA_IN_USE"},
    {ACAMERA_ERROR_CAMERA_DISABLED, "ACAMERA_ERROR_CAMERA_DISABLED"},
    {ACAMERA_ERROR_PERMISSION_DENIED, "ACAMERA_ERROR_PERMISSION_DENIED"},
}};

using DevErrorPair = std::pair<int, const char*>;
static constexpr std::array<DevErrorPair, 5> camera_device_error_info{{
    {ERROR_CAMERA_IN_USE, "ERROR_CAMERA_IN_USE"},
    {ERROR_MAX_CAMERAS_IN_USE, "ERROR_MAX_CAMERAS_IN_USE"},
    {ERROR_CAMERA_DISABLED, "ERROR_CAMERA_DISABLED"},
    {ERROR_CAMERA_DEVICE, "ERROR_CAMERA_DEVICE"},
    {ERROR_CAMERA_SERVICE, "ERROR_CAMERA_SERVICE"},
}};

constexpr auto GetErrorStr(camera_status_t err) -> const char* {
  return GetPairStr<camera_status_t, std::size(camera_error_info)>(err, camera_error_info);
}

constexpr auto GetCameraDeviceErrorStr(int err) -> const char* {
  return GetPairStr<int, std::size(camera_device_error_info)>(err, camera_device_error_info);
}

}  // namespace eillance
