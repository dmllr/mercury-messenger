#pragma once

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "camera/camera_codec.hpp"
#include "camera/camera_manager.hpp"
#include "rtc/audio_buffer_source.hpp"
#include "rtc/camera_buffer_source.hpp"
#include "rtc/rtc_producer.hpp"
#include "server/rtsp_manager.hpp"


/**
 * AppEngine
 */
class AppEngine {
  JNIEnv* env;
  jobject service_instance;
  jobject preview_surface = nullptr;
  std::shared_ptr<CameraManager> camera_manager;
  std::shared_ptr<RtcProducer> rtc_producer;
  std::shared_ptr<server::RtspManager> rtsp_manager;
  ImageFormat compatible_camera_res;

public:
  explicit AppEngine(JNIEnv* env, jobject instance)
      : env(env),
        service_instance(env->NewWeakGlobalRef(instance)) {
    memset(&compatible_camera_res, 0, sizeof(compatible_camera_res));

    camera_manager = std::make_shared<CameraManager>();

    // TODO: this is for the preview texture.
    /// width and height should be calculated as
    /// camera_manager->matchCaptureSizeRequest(texture_width, texture_height, &compatible_camera_res);
    compatible_camera_res.width = 1920;
    compatible_camera_res.height = 1080;
    compatible_camera_res.format = AIMAGE_FORMAT_YUV_420_888;

    const auto stream = createRtcStream();

    rtc_producer = std::make_shared<RtcProducer>();
    rtc_producer->setStream(stream);
    rtc_producer->startServer();

    rtsp_manager = std::make_shared<server::RtspManager>(camera_manager);
    rtsp_manager->start(camera_manager->codec()->getCodec());
  }

  ~AppEngine() {
    if (preview_surface) {
      env->DeleteGlobalRef(preview_surface);
      preview_surface = nullptr;
    }
  }

  const ImageFormat& getCompatibleCameraRes() const {
    return compatible_camera_res;
  }

  int32_t getCameraSensorOrientation(int32_t requestFacing) {
    int32_t facing = 0, angle = 0;
    if (camera_manager->getSensorOrientation(&facing, &angle) ||
        facing == requestFacing) {
      return angle;
    }
    return 0;
  }

  jobject getSurfaceObject() {
    return preview_surface;
  }

  void addPreviewSurface(jobject surface) {
    if (preview_surface)
      env->DeleteGlobalRef(preview_surface);

    preview_surface = env->NewGlobalRef(surface);
  }

  void removePreviewSurface() {
    if (preview_surface) {
      env->DeleteGlobalRef(preview_surface);
      preview_surface = nullptr;
    }

    camera_manager->removeCaptureRequestTarget(CaptureTarget::PREVIEW_SURFACE);
  }

  void requestPreview(bool start) {
    static constexpr auto target = CaptureTarget::PREVIEW_SURFACE;

    if (start) {
      camera_manager->registerSurfaceForTarget(target, ANativeWindow_fromSurface(env, preview_surface));
      camera_manager->addConsumer(target);
    } else {
      camera_manager->removeConsumer(target);
    }
  }

  void requestRtc() {
    rtc_producer->startServer();
  }


protected:
  std::shared_ptr<Stream> createRtcStream() {
    auto video = std::make_shared<CameraBufferSource>(camera_manager);
    auto audio = std::make_shared<AudioBufferSource>();

    return std::make_shared<Stream>(video, audio);
  }
};
