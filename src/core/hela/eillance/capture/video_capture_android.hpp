#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <jni.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <rtc/common.hpp>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/capture/video_encoder_mediandk.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/device/android/camera_utils.hpp"
#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

class VideoCapture {
  ACameraManager* camera_manager_{nullptr};
  ACameraDevice* cameraDevice{nullptr};
  ACameraCaptureSession* capture_session{nullptr};
  ACameraOutputTarget* target{nullptr};
  ACaptureSessionOutputContainer* output_container{nullptr};
  ACaptureSessionOutput* session_output{nullptr};
  ACaptureRequest* capture_request{nullptr};

  VideoConfig config_;
  VideoEncoder encoder_;
  EventQueue::SubscriberTypeId configure_subscriber_id_;
  std::mutex capture_mutex_;

public:
  VideoCapture() {
    configure_subscriber_id_ = EventQueue::Instance().Subscribe<VideoConfig>(
        [this](VideoConfig config) { ConfigureVideo(std::move(config)); });
  }
  VideoCapture(VideoCapture&&) = delete;
  VideoCapture(const VideoCapture&) = delete;
  VideoCapture& operator=(VideoCapture&&) = delete;
  VideoCapture& operator=(const VideoCapture&) = delete;

  ~VideoCapture() {
    EventQueue::Instance().Unsubscribe<const VideoConfig>(configure_subscriber_id_);
    Revoke();
  }

  void Engage() {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    camera_manager_ = ACameraManager_create();
    if (camera_manager_ == nullptr) {
      Log::e("Failed to create camera manager");
      return;
    }
    encoder_.Engage(config_);
    RenewCaptureSession();
  }

  void Revoke() {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    encoder_.Revoke();
    FreeNativeCameraResources();
    if (camera_manager_ != nullptr) {
      ACameraManager_delete(camera_manager_);
      camera_manager_ = nullptr;
    }
  }

  Packet FetchBuffers() {
    return encoder_.EncodeBuffer();
  }

  Time::Unit GetSampleDuration() {
    return Time::uS / config_.frameRate;
  }

  void ConfigureVideo(VideoConfig config) {
    TELEMETRY(
        LOGI << "Video capture configuration event triggered: device " << config.cameraId << " " << config.width << "x" << config.height << " " << config.frameRate << "fps";)
    std::lock_guard<std::mutex> lock(capture_mutex_);
    config_ = std::move(config);

    // Ensure default values are set
    if (config_.width == 0 || config_.height == 0) {
      config_.width = VideoConfigDefaults::width;
      config_.height = VideoConfigDefaults::height;
    }
    if (config_.frameRate == 0) {
      config_.frameRate = VideoConfigDefaults::frameRate;
    }
    if (config_.bitrate == 0) {
      config_.bitrate = static_cast<int>(VideoConfigDefaults::commonBitrateMultiplier * config_.width * config_.height * config_.frameRate);
    }

    encoder_.UpdateConfig(config_);

    if (camera_manager_ != nullptr) {
      FreeNativeCameraResources();
      RenewCaptureSession();
    }
  }

private:
  void OpenCamera() {
    FreeNativeCameraResources();

    ACameraIdList* cameraIdList = nullptr;
    auto status = ACameraManager_getCameraIdList(camera_manager_, &cameraIdList);
    if (status != ACAMERA_OK) {
      Log::e("Failed to get camera ID list");
      ACameraManager_delete(camera_manager_);
      return;
    }
    if (cameraIdList->numCameras <= 0) {
      Log::e("No camera available");
      return;
    }

    if (config_.cameraId >= cameraIdList->numCameras) {
      Log::e("Specified camera ID %d is out of range, max available ID is %d", config_.cameraId, cameraIdList->numCameras - 1);
      ACameraManager_deleteCameraIdList(cameraIdList);
      return;
    }

    auto* cameraId = cameraIdList->cameraIds[config_.cameraId];
    status = ACameraManager_openCamera(camera_manager_, cameraId, GetDeviceListener(), &cameraDevice);
    if (status != ACAMERA_OK) {
      Log::e("ACameraManager_openCamera call failed with code: %#x, %s", status, GetErrorStr(status));
      ACameraManager_deleteCameraIdList(cameraIdList);
      return;
    }

    ACameraManager_deleteCameraIdList(cameraIdList);
    Log::i("Camera %d opened successfully", config_.cameraId);
  }

  void RenewCaptureSession() {
    OpenCamera();

    auto status = ACaptureSessionOutputContainer_create(&output_container);
    Log::assert_or_die(status == ACAMERA_OK, "ACaptureSessionOutputContainer_create call failed with code: %#x, %s", status, GetErrorStr(status));

    auto* native_window = encoder_.Surface();
    ANativeWindow_acquire(native_window);

    status = ACaptureSessionOutput_create(native_window, &session_output);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureSessionOutputContainer_add(output_container, session_output);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraOutputTarget_create(native_window, &target);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_RECORD, &capture_request);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureRequest_addTarget(capture_request, target);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraDevice_createCaptureSession(
        cameraDevice,
        output_container,
        GetSessionListener(),
        &capture_session);
    Log::assert_or_die(status == ACAMERA_OK, "ACameraDevice_createCaptureSession failed with status: %#x, %s", status, GetErrorStr(status));

    status = ACameraCaptureSession_setRepeatingRequest(capture_session, nullptr, 1, &capture_request, nullptr);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to set repeating request with status: %#x, %s", status, GetErrorStr(status));
  }

  ACameraDevice_stateCallbacks* GetDeviceListener() {
    static ACameraDevice_stateCallbacks cameraDeviceListener = {
        .context = this,
        .onDisconnected = [](void* ctx, ACameraDevice* dev) { reinterpret_cast<VideoCapture*>(ctx)->onDeviceDisconnected(dev); },
        .onError = [](void* ctx, ACameraDevice* dev, int err) { reinterpret_cast<VideoCapture*>(ctx)->onDeviceError(dev, err); },
    };
    return &cameraDeviceListener;
  }

  void onDeviceDisconnected(ACameraDevice* dev) {
    auto id{ACameraDevice_getId(dev)};
    Log::w("Device %s is disconnected", id);

    if (cameraDevice != nullptr) {
      std::lock_guard<std::mutex> lock(capture_mutex_);
      ACameraDevice_close(cameraDevice);
      cameraDevice = nullptr;
    }
  }

  void onDeviceError(ACameraDevice* dev, int err) {
    auto id{ACameraDevice_getId(dev)};

    Log::e("CameraDevice %s is in error %#x", id, err);
    Log::e("CameraDeviceError(%#x): %s", err, GetCameraDeviceErrorStr(err));

    switch (err) {
      case ERROR_CAMERA_IN_USE:
        break;
      case ERROR_CAMERA_SERVICE:
      case ERROR_CAMERA_DEVICE:
      case ERROR_CAMERA_DISABLED:
      case ERROR_MAX_CAMERAS_IN_USE:
        break;
      default:
        Log::i("Unknown Camera Device Error: %#x", err);
    }
  }

  ACameraCaptureSession_stateCallbacks* GetSessionListener() {
    static ACameraCaptureSession_stateCallbacks sessionListener = {
        .context = this,
    };
    return &sessionListener;
  }

  void FreeNativeCameraResources() {
    if (output_container != nullptr) {
      ACaptureSessionOutputContainer_free(output_container);
      output_container = nullptr;
    }
    if (session_output != nullptr) {
      ACaptureSessionOutput_free(session_output);
      session_output = nullptr;
    }
    if (target != nullptr) {
      ACameraOutputTarget_free(target);
      target = nullptr;
    }
    if (capture_request != nullptr) {
      ACaptureRequest_free(capture_request);
      capture_request = nullptr;
    }
    if (cameraDevice != nullptr) {
      ACameraDevice_close(cameraDevice);
      cameraDevice = nullptr;
    }
    capture_session = nullptr;
  }
};

}  // namespace hela::eillance
