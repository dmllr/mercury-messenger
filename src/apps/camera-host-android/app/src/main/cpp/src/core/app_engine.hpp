#pragma once

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "camera/camera_codec.hpp"
#include "camera/camera_manager.hpp"

#include "events/event_source.h"

#include "impl/rtsp/rtsp_manager.hpp"
#include "impl/rtc/audio_buffer_source.hpp"
#include "impl/rtc/camera_buffer_source.hpp"
#include "core/rtc/rtc_producer.hpp"
#include "core/stream/video_stream_source.hpp"


/**
 * AppEngine
 */
class AppEngine {
  JavaVM* jvm;
  JNIEnv* env;
  jobject service_instance;
  jobject status_update_callback;
  jobject preview_surface = nullptr;
  std::shared_ptr<CameraManager> camera_manager;
  std::shared_ptr<RtcProducer> rtc_producer;
  std::shared_ptr<rtsp::RtspManager> rtsp_manager;
  ImageFormat compatible_camera_res;

public:
  explicit AppEngine(JavaVM* jvm, JNIEnv* env, jobject instance, jobject status_update_callback)
      : jvm(jvm),
        env(env),
        service_instance(env->NewWeakGlobalRef(instance)),
        status_update_callback(env->NewGlobalRef(status_update_callback)) {
    memset(&compatible_camera_res, 0, sizeof(compatible_camera_res));

    camera_manager = std::make_shared<CameraManager>();

    // TODO: this is for the preview texture.
    /// width and height should be calculated as
    /// camera_manager->matchCaptureSizeRequest(texture_width, texture_height, &compatible_camera_res);
    compatible_camera_res.width = 1920;
    compatible_camera_res.height = 1080;
    compatible_camera_res.format = AIMAGE_FORMAT_YUV_420_888;

    const auto stream = createRtcStream();

//    stream->start();

    rtc_producer = std::make_shared<RtcProducer>();
    rtc_producer->setStream(stream);
    rtc_producer->setStatusCallback([this](EventSource sender, int status) { setStatus(sender, status); });
    rtc_producer->startServer();

    rtsp_manager = std::make_shared<rtsp::RtspManager>(camera_manager);
    rtsp_manager->start(camera_manager->codec()->getCodec());
  }

  ~AppEngine() {
    if (preview_surface) {
      env->DeleteGlobalRef(preview_surface);
      preview_surface = nullptr;
    }
    env->DeleteGlobalRef(service_instance);
    env->DeleteGlobalRef(status_update_callback);
  }

  const ImageFormat& getCompatibleCameraRes() const {
    return compatible_camera_res;
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

void setStatus(EventSource sender, int status) {
    JNIEnv* env = attachCurrentThreadIfNeeded();
    if(env == nullptr) {
        return;
    }
    jclass callbackClass = env->GetObjectClass(status_update_callback);
    jmethodID callbackMethod = env->GetMethodID(callbackClass, "onStatusUpdate", "(II)V");
    if (callbackMethod != nullptr) {
        env->CallVoidMethod(status_update_callback, callbackMethod, static_cast<int>(sender), status);
    }
}

JNIEnv* attachCurrentThreadIfNeeded() {
    JNIEnv *env;
    jint res = jvm->GetEnv((void **)&env, JNI_VERSION_1_6);
    switch (res) {
        case JNI_OK:
            // the current thread is already attached, return the JNIEnv.
            return env;
        case JNI_EDETACHED:
            // the current thread is not attached, try to attach.
            if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                return env;
            } else {
                // failed to attach, handle error.
                return nullptr;
            }
        default:
            // unknown error, handle error.
            return nullptr;
    }
}

protected:
  std::shared_ptr<Stream> createRtcStream() {
    auto video = std::make_shared<VideoStreamSource>();
    auto audio = std::make_shared<AudioBufferSource>();

    return std::make_shared<Stream>(video, audio);
  }
};
