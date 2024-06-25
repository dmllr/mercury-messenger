#pragma once

#include <cinttypes>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImage.h>

#include "hela/eillance/logging/logging.h"


namespace hela::eillance {

auto GetAvailableCameras() -> std::vector<std::pair<std::string, std::string>> {
  ACameraManager* cameraManager = ACameraManager_create();
  if (!cameraManager) {
    LOGE << "Failed to create camera manager";
    return {};
  }

  ACameraIdList* cameraIdList = nullptr;
  if (ACameraManager_getCameraIdList(cameraManager, &cameraIdList) != ACAMERA_OK) {
    LOGE << "Failed to get camera ID list";
    ACameraManager_delete(cameraManager);
    return {};
  }

  std::vector<std::pair<std::string, std::string>> cameras;
  for (int i = 0; i < cameraIdList->numCameras; ++i) {
    const char* cameraId = cameraIdList->cameraIds[i];

    ACameraMetadata* metadata = nullptr;
    if (ACameraManager_getCameraCharacteristics(cameraManager, cameraId, &metadata) != ACAMERA_OK) {
      LOGE << "Failed to get camera characteristics for camera ID: " << cameraId;
      continue;
    }

    ACameraMetadata_const_entry entry;
    if (ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry) == ACAMERA_OK) {
      const char* facing = (entry.data.u8[0] == ACAMERA_LENS_FACING_FRONT) ? "Front" : "Back";
      cameras.push_back({cameraId, facing});
    }

    ACameraMetadata_free(metadata);
  }

  ACameraManager_deleteCameraIdList(cameraIdList);
  ACameraManager_delete(cameraManager);

  return cameras;
}

auto GetSupportedResolutions(const std::string& cameraId) -> std::vector<std::pair<int32_t, int32_t>> {
  ACameraManager* cameraManager = ACameraManager_create();
  if (!cameraManager) {
    LOGE << "Failed to create camera manager";
    return {};
  }

  ACameraMetadata* metadata = nullptr;
  if (ACameraManager_getCameraCharacteristics(cameraManager, cameraId.c_str(), &metadata) != ACAMERA_OK) {
    LOGE << "Failed to get camera characteristics for camera ID: " << cameraId.c_str();
    ACameraManager_delete(cameraManager);
    return {};
  }

  ACameraMetadata_const_entry entry;
  std::vector<std::pair<int32_t, int32_t>> resolutions;
  if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
    for (uint32_t i = 0; i < entry.count; i += 4) {
      if (entry.data.i32[i + 3] == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
        resolutions.emplace_back(entry.data.i32[i + 1], entry.data.i32[i + 2]);
      }
    }
  }

  ACameraMetadata_free(metadata);
  ACameraManager_delete(cameraManager);

  return resolutions;
}

auto GetDeviceProfileAsJson() -> nlohmann::json {
  using Json = nlohmann::json;
  auto cameras = GetAvailableCameras();
  auto cameras_json = Json::array();

  for (const auto& camera : cameras) {
    auto camera_json{Json()};
    camera_json["id"] = camera.first;
    camera_json["name"] = camera.second;

    auto resolutions = GetSupportedResolutions(camera.first);
    auto resolutions_json{Json::array()};
    for (const auto& res : resolutions) {
      resolutions_json.push_back({{"width", res.first}, {"height", res.second}});
    }

    camera_json["resolutions"] = resolutions_json;
    cameras_json.push_back(camera_json);
  }

  return cameras_json;
}

template<typename T>
const char* GetPairStr(T key, std::vector<std::pair<T, const char*>>& store) {
  for (auto it = store.begin(); it != store.end(); ++it) {
    if (it->first == key) {
      return it->second;
    }
  }
  Log::w("(%#08x) : UNKNOWN TAG for %s", key, typeid(store[0].first).name());
  return "UNKNOWN";
}

using ERROR_PAIR = std::pair<camera_status_t, const char*>;
static std::vector<ERROR_PAIR> CameraErrorInfo{
    std::make_pair(ACAMERA_OK, "ACAMERA_OK"),
    std::make_pair(ACAMERA_ERROR_UNKNOWN, "ACAMERA_ERROR_UNKNOWN"),
    std::make_pair(ACAMERA_ERROR_INVALID_PARAMETER, "ACAMERA_ERROR_INVALID_PARAMETER"),
    std::make_pair(ACAMERA_ERROR_CAMERA_DISCONNECTED, "ACAMERA_ERROR_CAMERA_DISCONNECTED"),
    std::make_pair(ACAMERA_ERROR_NOT_ENOUGH_MEMORY, "ACAMERA_ERROR_NOT_ENOUGH_MEMORY"),
    std::make_pair(ACAMERA_ERROR_METADATA_NOT_FOUND, "ACAMERA_ERROR_METADATA_NOT_FOUND"),
    std::make_pair(ACAMERA_ERROR_CAMERA_DEVICE, "ACAMERA_ERROR_CAMERA_DEVICE"),
    std::make_pair(ACAMERA_ERROR_CAMERA_SERVICE, "ACAMERA_ERROR_CAMERA_SERVICE"),
    std::make_pair(ACAMERA_ERROR_SESSION_CLOSED, "ACAMERA_ERROR_SESSION_CLOSED"),
    std::make_pair(ACAMERA_ERROR_INVALID_OPERATION, "ACAMERA_ERROR_INVALID_OPERATION"),
    std::make_pair(ACAMERA_ERROR_STREAM_CONFIGURE_FAIL, "ACAMERA_ERROR_STREAM_CONFIGURE_FAIL"),
    std::make_pair(ACAMERA_ERROR_CAMERA_IN_USE, "ACAMERA_ERROR_CAMERA_IN_USE"),
    std::make_pair(ACAMERA_ERROR_MAX_CAMERA_IN_USE, "ACAMERA_ERROR_MAX_CAMERA_IN_USE"),
    std::make_pair(ACAMERA_ERROR_CAMERA_DISABLED, "ACAMERA_ERROR_CAMERA_DISABLED"),
    std::make_pair(ACAMERA_ERROR_PERMISSION_DENIED, "ACAMERA_ERROR_PERMISSION_DENIED"),
};

using DEV_ERROR_PAIR = std::pair<int, const char*>;
static std::vector<DEV_ERROR_PAIR> CameraDeviceErrorInfo{
    std::make_pair(ERROR_CAMERA_IN_USE, "ERROR_CAMERA_IN_USE"),
    std::make_pair(ERROR_MAX_CAMERAS_IN_USE, "ERROR_MAX_CAMERAS_IN_USE"),
    std::make_pair(ERROR_CAMERA_DISABLED, "ERROR_CAMERA_DISABLED"),
    std::make_pair(ERROR_CAMERA_DEVICE, "ERROR_CAMERA_DEVICE"),
    std::make_pair(ERROR_CAMERA_SERVICE, "ERROR_CAMERA_SERVICE"),
};

inline const char* GetErrorStr(camera_status_t err) {
  return GetPairStr<camera_status_t>(err, CameraErrorInfo);
}

inline const char* GetCameraDeviceErrorStr(int err) {
  return GetPairStr<int>(err, CameraDeviceErrorInfo);
}

}  // namespace hela::eillance
