#pragma once

#include <string>
#include <vector>
#include <mutex>

#include <jni.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <rtc/common.hpp>

#include "hela/eillance/utils/android-camera_utils.hpp"
#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/capture/video_encoder_mediandk.hpp"
#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/units.hpp"


namespace hela::eillance {

class VideoCapture {
  JNIEnv* env_{nullptr};
  ACameraManager* camera_manager_{nullptr};
  ACameraDevice *cameraDevice{nullptr};
  ACaptureSessionOutputContainer* output_container;
  ACameraCaptureSession* capture_session;

  VideoEncoder encoder_;

public:
  VideoCapture() = default;

  ~VideoCapture() {
    Revoke();
  }

  void Engage() {
    camera_manager_ = ACameraManager_create();
    if (camera_manager_ == nullptr) {
      Log::e("Failed to create camera manager");
      return;
    }
    encoder_.Engage();
  }

//  void CaptureFrame() {
//    dispatch_queue_.dispatch([this]() {
//      CaptureFrame();
//    });
//  }

  Packet FetchBuffers() {
//    auto frame = cv_frame_;
//    cv::cvtColor(frame, frame, cv::COLOR_BGR2YUV_YV12);
//    YV12toNV12(frame, nv12_frame_);
//
//    return encoder_.EncodeBuffer(nv12_frame_.data);
    return encoder_.EncodeBuffer();
  }

  void Revoke() {
    encoder_.Revoke();
    ACameraCaptureSession_stopRepeating(capture_session);
    ACameraManager_delete(camera_manager_);
    camera_manager_ = nullptr;
  }

private:
  void OpenCamera() {
    // get camera ID list
    ACameraIdList *cameraIdList = nullptr;
    auto status = ACameraManager_getCameraIdList(camera_manager_, &cameraIdList);
    if (status != ACAMERA_OK) {
      Log::e( "Failed to get camera ID list");
      ACameraManager_delete(camera_manager_);
      return;
    }
    if (cameraIdList->numCameras <= 0) {
      Log::e("No camera available");
      return;
    }
    
    // open first available camera
    auto* cameraId = cameraIdList->cameraIds[0];
    status = ACameraManager_openCamera(camera_manager_, cameraId, nullptr, &cameraDevice);
    if (status != ACAMERA_OK) {
      Log::e("ACameraManager_openCamera call failed with code: %#x, %s", status, GetErrorStr(status));
      return;
    }

    // free camera list
    ACameraManager_deleteCameraIdList(cameraIdList);
  }

  void renewCaptureSession() {
    OpenCamera();

    auto status = ACaptureSessionOutputContainer_create(&output_container);
    Log::assert_or_die(status == ACAMERA_OK, "ACaptureSessionOutputContainer_create call failed with code: %#x, %s", status, GetErrorStr(status));

    auto* native_window = encoder_.Surface();
    ANativeWindow_acquire(native_window);

    ACaptureSessionOutput* session_output;
    status = ACaptureSessionOutput_create(native_window, &session_output);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureSessionOutputContainer_add(output_container, session_output);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    ACameraOutputTarget* target;
    status = ACameraOutputTarget_create(native_window, &target);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    ACaptureRequest* capture_request;
    status = ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_RECORD, &capture_request);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    status = ACaptureRequest_addTarget(capture_request, target);
    Log::assert_or_die(status == ACAMERA_OK, "renewCaptureSession failed with code: %#x, %s ", status, GetErrorStr(status));

    // Create a capture session for the given preview requests
//    capture_session_state = CaptureSessionState::READY;
    status = ACameraDevice_createCaptureSession(
        cameraDevice,
        output_container,
        GetSessionListener(),
        &capture_session);
    Log::assert_or_die(status == ACAMERA_OK, "ACameraDevice_createCaptureSession failed with status: %#x, %s", status, GetErrorStr(status));

    // Define a capture request
    ACaptureRequest* captureRequest = nullptr;
    status = ACameraDevice_createCaptureRequest(cameraDevice, TEMPLATE_RECORD, &captureRequest);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to create capture request with status: %#x, %s", status, GetErrorStr(status));

    // Add target to the capture request
    status = ACaptureRequest_addTarget(captureRequest, target);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to add target to capture request with status: %#x, %s", status, GetErrorStr(status));

    // Set repeating request for the capture session
    status = ACameraCaptureSession_setRepeatingRequest(capture_session, nullptr, 1, &captureRequest, nullptr);
    Log::assert_or_die(status == ACAMERA_OK, "Failed to set repeating request with status: %#x, %s", status, GetErrorStr(status));
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

};

}  // namespace hela::eillance
