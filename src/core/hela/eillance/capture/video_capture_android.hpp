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

public:
  VideoCapture(){
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
    camera_manager_ = ACameraManager_create();
    if (camera_manager_ == nullptr) {
      Log::e("Failed to create camera manager");
      return;
    }
    encoder_.Engage(config_);
    renewCaptureSession();
  }

  void Revoke() {
    encoder_.Revoke();
    ACameraCaptureSession_stopRepeating(capture_session);
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
    config_ = std::move(config);
    if (capture_session != nullptr) {
      ACameraCaptureSession_stopRepeating(capture_session);
      renewCaptureSession();
    }
    encoder_.UpdateConfig(config_);
  }

private:
  void OpenCamera() {
    FreeNativeCameraResources();

    // get camera ID list
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

    // Check if the specified camera ID is within valid range
    if (config_.cameraId >= cameraIdList->numCameras) {
      Log::e("Specified camera ID %d is out of range, max available ID is %d", config_.cameraId, cameraIdList->numCameras - 1);
      ACameraManager_deleteCameraIdList(cameraIdList);
      return;
    }

    // Open specified camera by config_.cameraId
    auto* cameraId = cameraIdList->cameraIds[config_.cameraId];
    status = ACameraManager_openCamera(camera_manager_, cameraId, GetDeviceListener(), &cameraDevice);
    if (status != ACAMERA_OK) {
      Log::e("ACameraManager_openCamera call failed with code: %#x, %s", status, GetErrorStr(status));
      ACameraManager_deleteCameraIdList(cameraIdList);
      return;
    }

    // Free camera list
    ACameraManager_deleteCameraIdList(cameraIdList);
    Log::i("Camera %d opened successfully", config_.cameraId);
  }

  void renewCaptureSession() {
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

    // Add target to the capture request
    status = ACaptureRequest_addTarget(capture_request, target);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to add target to capture request with status: %#x, %s", status, GetErrorStr(status));

    // Set repeating request for the capture session
    status = ACameraCaptureSession_setRepeatingRequest(capture_session, nullptr, 1, &capture_request, nullptr);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to set repeating request with status: %#x, %s", status, GetErrorStr(status));
  }

  /**
   * Device State Callbacks
   * @return ACameraDevice_stateCallbacks
   */
  ACameraDevice_stateCallbacks* GetDeviceListener() {
    static ACameraDevice_stateCallbacks cameraDeviceListener = {
        .context = this,
        .onDisconnected = [](void* ctx, ACameraDevice* dev) { reinterpret_cast<VideoCapture*>(ctx)->onDeviceState(dev); },
        .onError = [](void* ctx, ACameraDevice* dev, int err) { reinterpret_cast<VideoCapture*>(ctx)->onDeviceError(dev, err); },
    };
    return &cameraDeviceListener;
  }

  void onDeviceState(ACameraDevice* dev) {
    auto id{ACameraDevice_getId(dev)};
    Log::w("Device %s is disconnected", id);

    if (cameraDevice != nullptr) {
      ACameraDevice_close(cameraDevice);
    }
  }

  void onDeviceError(ACameraDevice* dev, int err) {
    auto id{ACameraDevice_getId(dev)};

    Log::e("CameraDevice %s is in error %#x", id, err);
    Log::e("CameraDeviceError(%#x): %s", err, GetCameraDeviceErrorStr(err));

    //    CameraId& cam = cameras[id];

    switch (err) {
      case ERROR_CAMERA_IN_USE:
        //        cam.available = false;
        //        cam.owner = false;
        break;
      case ERROR_CAMERA_SERVICE:
      case ERROR_CAMERA_DEVICE:
      case ERROR_CAMERA_DISABLED:
      case ERROR_MAX_CAMERAS_IN_USE:
        //        cam.available = false;
        //        cam.owner = false;
        break;
      default:
        Log::i("Unknown Camera Device Error: %#x", err);
    }
  }

  /**
   * Capture Session Callbacks
   * @return ACameraCaptureSession_stateCallbacks
   */
  ACameraCaptureSession_stateCallbacks* GetSessionListener() {
    static ACameraCaptureSession_stateCallbacks sessionListener = {
        .context = this,
        //        .onClosed = [](void* ctx, ACameraCaptureSession* ses) {
        //          Log::w("Camera Capture Session %p closed", ses);
        //          reinterpret_cast<VideoCapture*>(ctx)->onSessionState(ses, CaptureSessionState::CLOSED); },
        //        .onReady = [](void* ctx, ACameraCaptureSession* ses) {
        //          Log::w("Camera Capture Session %p ready", ses);
        //          auto ths = reinterpret_cast<VideoCapture*>(ctx);
        //          ths->finishClosing();
        //          ths->onSessionState(ses, CaptureSessionState::READY); },
        //        .onActive = [](void* ctx, ACameraCaptureSession* ses) {
        //          Log::w("Camera Capture Session %p active", ses);
        //          reinterpret_cast<VideoCapture*>(ctx)->onSessionState(ses, CaptureSessionState::ACTIVE); },
    };
    return &sessionListener;
  }

  //  /**
  //   * Handles capture session state changes.
  //   *   Update into internal session state.
  //   */
  //  void onSessionState(ACameraCaptureSession* ses, CaptureSessionState state) {
  //    if (!ses || ses != capture_session) {
  //      Log::w("CaptureSession is %s", (ses ? "NOT our session" : "NULL"));
  //      return;
  //    }
  //
  //    capture_session_state = state;
  //  }

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
    if (capture_session != nullptr) {
      ACameraCaptureSession_close(capture_session);
      capture_session = nullptr;
    }
    if (cameraDevice != nullptr) {
      ACameraDevice_close(cameraDevice);
      cameraDevice = nullptr;
    }
  }

};

}  // namespace hela::eillance
