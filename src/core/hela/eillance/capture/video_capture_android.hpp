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

#include "hela/eillance/capture/config.hpp"
#include "hela/eillance/capture/video_encoder_mediandk.hpp"
#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/device/android/camera_utils.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

class VideoCapture {
  ACameraManager* camera_manager_{nullptr};
  ACameraDevice* cameraDevice_{nullptr};
  ACameraCaptureSession* capture_session_{nullptr};
  ACameraOutputTarget* target_{nullptr};
  ACaptureSessionOutputContainer* output_container_{nullptr};
  ACaptureSessionOutput* session_output_{nullptr};
  ACaptureRequest* capture_request_{nullptr};

  VideoConfig config_;
  VideoEncoder encoder_;
  EventQueue::SubscriberTypeId configure_subscriber_id_;
  std::mutex capture_mutex_;

public:
  VideoCapture() {
    configure_subscriber_id_ = EventQueue::Instance().Subscribe<VideoConfig>(
        [this](VideoConfig config) { ConfigureVideo(config); });
  }
  VideoCapture(VideoCapture&&) = delete;
  VideoCapture(const VideoCapture&) = delete;
  VideoCapture& operator=(VideoCapture&&) = delete;
  VideoCapture& operator=(const VideoCapture&) = delete;

  ~VideoCapture() {
    EventQueue::Instance().Unsubscribe<VideoConfig>(configure_subscriber_id_);
    Revoke();
  }

  void Engage() {
    const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
    camera_manager_ = ACameraManager_create();
    if (camera_manager_ == nullptr) {
      Log::e("Failed to create camera manager");
      return;
    }
    encoder_.Engage(config_);
    RenewCaptureSession();
  }

  void Revoke() {
    const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
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

  [[nodiscard]] Time::Unit GetSampleDuration() const {
    return Time::uS / config_.frameRate;
  }

  void Configure(Config config) {
    ConfigureVideo(config.video_config);
  }

  void ConfigureVideo(VideoConfig config) {
    TELEMETRY(
        LOGI << "Video capture configuration event triggered: device " << config.cameraId << " " << config.width << "x" << config.height << " " << config.frameRate << "fps";)
    const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
    config_ = config;

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

    ACameraIdList* camera_id_list = nullptr;
    auto status = ACameraManager_getCameraIdList(camera_manager_, &camera_id_list);
    if (status != ACAMERA_OK) {
      Log::e("Failed to get camera ID list");
      ACameraManager_delete(camera_manager_);
      return;
    }
    if (camera_id_list->numCameras <= 0) {
      Log::e("No camera available");
      return;
    }

    if (config_.cameraId >= camera_id_list->numCameras) {
      Log::e("Specified camera ID %d is out of range, max available ID is %d", config_.cameraId, camera_id_list->numCameras - 1);
      ACameraManager_deleteCameraIdList(camera_id_list);
      return;
    }

    auto* camera_id = camera_id_list->cameraIds[config_.cameraId];
    status = ACameraManager_openCamera(camera_manager_, camera_id, GetDeviceListener(), &cameraDevice_);
    if (status != ACAMERA_OK) {
      Log::e("ACameraManager_openCamera call failed with code: %#x, %s", status, GetErrorStr(status));
      ACameraManager_deleteCameraIdList(camera_id_list);
      return;
    }

    ACameraManager_deleteCameraIdList(camera_id_list);
    Log::i("Camera %d opened successfully", config_.cameraId);
  }

  void RenewCaptureSession() {
    OpenCamera();

    auto status = ACaptureSessionOutputContainer_create(&output_container_);
    Log::assert_or_die(status == ACAMERA_OK, "ACaptureSessionOutputContainer_create call failed with code: %#x, %s", status, GetErrorStr(status));

    auto* native_window = encoder_.Surface();
    ANativeWindow_acquire(native_window);

    status = ACaptureSessionOutput_create(native_window, &session_output_);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureSessionOutputContainer_add(output_container_, session_output_);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraOutputTarget_create(native_window, &target_);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraDevice_createCaptureRequest(cameraDevice_, TEMPLATE_RECORD, &capture_request_);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureRequest_addTarget(capture_request_, target_);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACameraDevice_createCaptureSession(
        cameraDevice_,
        output_container_,
        GetSessionListener(),
        &capture_session_);
    Log::assert_or_die(status == ACAMERA_OK, "ACameraDevice_createCaptureSession failed with status: %#x, %s", status, GetErrorStr(status));

    status = ACameraCaptureSession_setRepeatingRequest(capture_session_, nullptr, 1, &capture_request_, nullptr);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to set repeating request with status: %#x, %s", status, GetErrorStr(status));
  }

  ACameraDevice_stateCallbacks* GetDeviceListener() {
    static ACameraDevice_stateCallbacks cameraDeviceListener = {
        .context = this,
        .onDisconnected = [](void* ctx, ACameraDevice* dev) { reinterpret_cast<VideoCapture*>(ctx)->OnDeviceDisconnected(dev); },
        .onError = [](void* ctx, ACameraDevice* dev, int err) { reinterpret_cast<VideoCapture*>(ctx)->OnDeviceError(dev, err); },
    };
    return &cameraDeviceListener;
  }

  void OnDeviceDisconnected(ACameraDevice* dev) {
    const auto* id{ACameraDevice_getId(dev)};
    Log::w("Device %s is disconnected", id);

    if (cameraDevice_ != nullptr) {
      const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
      ACameraDevice_close(cameraDevice_);
      cameraDevice_ = nullptr;
    }
  }

  void OnDeviceError(ACameraDevice* dev, int err) {
    const auto* id{ACameraDevice_getId(dev)};

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
    static ACameraCaptureSession_stateCallbacks session_listener = {
        .context = this,
    };
    return &session_listener;
  }

  void FreeNativeCameraResources() {
    if (output_container_ != nullptr) {
      ACaptureSessionOutputContainer_free(output_container_);
      output_container_ = nullptr;
    }
    if (session_output_ != nullptr) {
      ACaptureSessionOutput_free(session_output_);
      session_output_ = nullptr;
    }
    if (target_ != nullptr) {
      ACameraOutputTarget_free(target_);
      target_ = nullptr;
    }
    if (capture_request_ != nullptr) {
      ACaptureRequest_free(capture_request_);
      capture_request_ = nullptr;
    }
    if (cameraDevice_ != nullptr) {
      ACameraDevice_close(cameraDevice_);
      cameraDevice_ = nullptr;
    }
    capture_session_ = nullptr;
  }
};

}  // namespace hela::eillance
