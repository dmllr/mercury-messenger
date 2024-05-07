#pragma once

#include <cinttypes>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImage.h>

#include "hela/eillance/logging/logging.h"


template<typename T>
const char* GetPairStr(T key, std::vector<std::pair<T, const char*>>& store) {
  typedef typename std::vector<std::pair<T, const char*>>::iterator iterator;
  for (iterator it = store.begin(); it != store.end(); ++it) {
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
