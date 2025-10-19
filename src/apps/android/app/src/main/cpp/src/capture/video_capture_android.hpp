#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <atomic>

#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <jni.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/sensor.h>

#include <rtc/common.hpp>

#include "eillance/utils/time.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/capture/orientation.hpp"
#include "eillance/logging/logging.h"
#include "eillance/rtc/stream.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/packet.hpp"
#include "video_encoder_android.hpp"
#include "../device/camera_utils.hpp"

namespace eillance {

class VideoCaptureAndroid {
  ACameraManager* camera_manager_{nullptr};
  ACameraDevice* cameraDevice_{nullptr};
  ACameraCaptureSession* capture_session_{nullptr};
  ACameraOutputTarget* target_{nullptr};
  ACaptureSessionOutputContainer* output_container_{nullptr};
  ACaptureSessionOutput* session_output_{nullptr};
  ACaptureRequest* capture_request_{nullptr};

  // Sensor-related members for orientation detection
  ASensorManager* sensor_manager_{nullptr};
  const ASensor* accelerometer_{nullptr};
  ASensorEventQueue* sensor_event_queue_{nullptr};
  std::atomic<DeviceOrientation> current_orientation_{DeviceOrientation::LandscapeToLeft};
  std::atomic<bool> orientation_monitoring_active_{false};

  VideoConfig config_;
  VideoEncoderAndroid encoder_;
  EventQueue::SubscriberTypeId configure_subscriber_id_;
  std::mutex capture_mutex_;

  // Thread for sensor monitoring
  std::thread orientation_thread_;

public:
  VideoCaptureAndroid() {
    configure_subscriber_id_ = EventQueue::Instance().Subscribe<VideoConfig>(
        [this](VideoConfig config) { ConfigureVideo(config); });
  }
  VideoCaptureAndroid(VideoCaptureAndroid&&) = delete;
  VideoCaptureAndroid(const VideoCaptureAndroid&) = delete;
  VideoCaptureAndroid& operator=(VideoCaptureAndroid&&) = delete;
  VideoCaptureAndroid& operator=(const VideoCaptureAndroid&) = delete;

  ~VideoCaptureAndroid() {
    EventQueue::Instance().Unsubscribe<VideoConfig>(configure_subscriber_id_);
    StopOrientationMonitoring();
    Revoke();
  }

  void Engage() {
    const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
    camera_manager_ = ACameraManager_create();
    if (camera_manager_ == nullptr) {
      Log::e("Failed to create camera manager");
      return;
    }

    StartOrientationMonitoring();
    encoder_.Engage(config_);
    RenewCaptureSession();
  }

  void Revoke() {
    const auto lock = std::lock_guard<std::mutex>(capture_mutex_);
    StopOrientationMonitoring();
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

  void Configure(const Config& config) {
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

    config_.rotation = static_cast<int>(current_orientation_.load());

    encoder_.UpdateConfig(config_);

    if (camera_manager_ != nullptr) {
      FreeNativeCameraResources();
      RenewCaptureSession();
    }
  }

private:
  void StartOrientationMonitoring() {
    if (orientation_monitoring_active_.load()) {
      return;
    }

    orientation_monitoring_active_ = true;
    orientation_thread_ = std::thread([this]() {
      sensor_manager_ = ASensorManager_getInstance();
      accelerometer_ = ASensorManager_getDefaultSensor(sensor_manager_, ASENSOR_TYPE_ACCELEROMETER);

      if (!accelerometer_) {
        Log::e("Failed to get accelerometer sensor");
        orientation_monitoring_active_ = false;
        return;
      }

      ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
      sensor_event_queue_ = ASensorManager_createEventQueue(
          sensor_manager_, looper, ALOOPER_POLL_CALLBACK, nullptr, nullptr);

      constexpr auto sensorSamplesPerSecond = 5;
      ASensorEventQueue_enableSensor(sensor_event_queue_, accelerometer_);
      ASensorEventQueue_setEventRate(sensor_event_queue_, accelerometer_,
                                     (1000L / sensorSamplesPerSecond) * 1000);

      auto last_orientation = current_orientation_.load();
      auto detected_orientation = last_orientation;

      auto orientation_stability_counter = 0;
      auto last_orientation_change = 0U;
      auto initial_orientation_stability_counter = 999;

      while (orientation_monitoring_active_.load()) {
        ASensorEvent event;
        while (ASensorEventQueue_getEvents(sensor_event_queue_, &event, 1) > 0) {
          if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            float x = event.acceleration.x;
            float y = event.acceleration.y;

            DeviceOrientation new_orientation;

            // Determine orientation from accelerometer readings
            if (std::abs(x) > std::abs(y)) {
              // Landscape orientation
              new_orientation = (x > 0) ? DeviceOrientation::LandscapeToLeft : DeviceOrientation::LandscapeToRight;
            } else {
              // Portrait orientation
              new_orientation = (y > 0) ? DeviceOrientation::Portrait : DeviceOrientation::UpsideDown;
            }

            // Check if orientation detection is stable
            if (new_orientation != detected_orientation) {
              detected_orientation = new_orientation;
              orientation_stability_counter = initial_orientation_stability_counter;
              initial_orientation_stability_counter = 0;
            } else {
              orientation_stability_counter++;
            }

            // Only update if orientation is stable for multiple readings and enough time has passed
            constexpr auto stabilityThreshold = sensorSamplesPerSecond * 2;
            constexpr auto minChangeInterval = Time::Unit(2U * 1000U * 1000U);  // 2s
            if (orientation_stability_counter >= stabilityThreshold &&
                detected_orientation != last_orientation &&
                Time::Now() - last_orientation_change > minChangeInterval) {

              current_orientation_ = detected_orientation;
              last_orientation = detected_orientation;
              last_orientation_change = Time::Now();
              orientation_stability_counter = 0;

              config_.rotation = static_cast<int>(new_orientation);

              DispatchQueue::MainThread().Dispatch([this]() {
                  ConfigureVideo(config_);
                });

              Log::i("Device orientation changed to: %d degrees", static_cast<int>(new_orientation));
            }
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      if (sensor_event_queue_) {
        ASensorEventQueue_disableSensor(sensor_event_queue_, accelerometer_);
        ASensorManager_destroyEventQueue(sensor_manager_, sensor_event_queue_);
        sensor_event_queue_ = nullptr;
      }

      sensor_manager_ = nullptr;
      accelerometer_ = nullptr;
    });
  }

  void StopOrientationMonitoring() {
    if (orientation_monitoring_active_.load()) {
      orientation_monitoring_active_ = false;
      if (orientation_thread_.joinable()) {
        orientation_thread_.join();
      }
    }
  }

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
        .onDisconnected = [](void* ctx, ACameraDevice* dev) { reinterpret_cast<VideoCaptureAndroid*>(ctx)->OnDeviceDisconnected(dev); },
        .onError = [](void* ctx, ACameraDevice* dev, int err) { reinterpret_cast<VideoCaptureAndroid*>(ctx)->OnDeviceError(dev, err); },
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

}  // namespace eillance
