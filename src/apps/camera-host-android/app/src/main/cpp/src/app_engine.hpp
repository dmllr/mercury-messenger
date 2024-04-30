#pragma once

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "hela/eillance/events/event_source.h"
#include "hela/eillance/rtc/producer.hpp"

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>


namespace hela::eillance {

class AppEngine {
  JavaVM* jvm;
  JNIEnv* env;
  jobject service_instance;
  jobject status_update_callback;
  jobject preview_surface = nullptr;
  std::shared_ptr<hela::eillance::RtcTransport<hela::eillance::RtcProducer>> rtc_producer;

public:
  explicit AppEngine(JavaVM* jvm, JNIEnv* env, jobject instance, jobject status_update_callback)
      : jvm(jvm),
        env(env),
        service_instance(env->NewWeakGlobalRef(instance)),
        status_update_callback(env->NewGlobalRef(status_update_callback)) {
    rtc_producer = std::make_shared<hela::eillance::RtcTransport<hela::eillance::RtcProducer>>();
    rtc_producer->SetStatusCallback([this](EventSource sender, int status) { setStatus(sender, status); });
    rtc_producer->Start("server");
  }

  ~AppEngine() {
    if (preview_surface) {
      env->DeleteGlobalRef(preview_surface);
      preview_surface = nullptr;
    }
    env->DeleteGlobalRef(service_instance);
    env->DeleteGlobalRef(status_update_callback);
  }

  jobject getPreviewSurface() {
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
  }

  void requestPreview(bool start) {
    work = start;
    if (start) {
      window = ANativeWindow_fromSurface(env, preview_surface);
      if (window == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "Native", "Error obtaining native window");
        return;
      }

      // Open camera, initialize preview, and display using OpenCV
      cap.open(0, cv::CAP_ANDROID);
      cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('B','G','R','3'));
      cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
      cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
      if (!cap.isOpened()) {
        __android_log_print(ANDROID_LOG_ERROR, "Native", "Error opening camera");
        ANativeWindow_release(window);
        return;
      }

      // Start the frame processing thread
      frameProcessingThread = std::thread(&AppEngine::frameProcessingFunc, this);
    }
  }

  void setStatus(EventSource sender, int status) {
    JNIEnv* env = attachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;
    }
    jclass callbackClass = env->GetObjectClass(status_update_callback);
    jmethodID callbackMethod = env->GetMethodID(callbackClass, "onStatusUpdate", "(II)V");
    if (callbackMethod != nullptr) {
      env->CallVoidMethod(status_update_callback, callbackMethod, static_cast<int>(sender), status);
    }
  }

  JNIEnv* attachCurrentThreadIfNeeded() {
    JNIEnv* env;
    jint res = jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    switch (res) {
      case JNI_OK:
        // the current thread is already attached, return the JNIEnv.
        return env;
      case JNI_EDETACHED:
        // the current thread is not attached, try to attach.
        if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          return env;
        } else {
          return nullptr;
        }
      default:
        return nullptr;
    }
  }

private:
  ANativeWindow* window;
  cv::VideoCapture cap;
  std::thread frameProcessingThread;
  std::atomic<bool> work;

  void frameProcessingFunc() {
    cv::Mat frame;
    while (work) {
      cap >> frame;
      if (frame.empty()) {
        std::cerr << "Unexpected end of stream." << std::endl;
        continue;
      }

      // Convert frame to RGBA for display on Android surface
       cvtColor(frame, frame, cv::COLOR_BGR2RGBA);

      ANativeWindow_acquire(window);
      ANativeWindow_Buffer buffer;
      if (ANativeWindow_lock(window, &buffer, nullptr) == 0) {
        memcpy(buffer.bits, frame.data, frame.total() * frame.elemSize());
        ANativeWindow_unlockAndPost(window);
      }
      ANativeWindow_release(window);
    }
  }
};

}  // namespace hela::eillance
