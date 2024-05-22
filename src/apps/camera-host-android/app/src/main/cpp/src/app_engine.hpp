#pragma once

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "hela/eillance/events/event_source.h"
#include "hela/eillance/rtc/producer.hpp"
#include "hela/eillance/utils/string.hpp"

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>


namespace hela::eillance {

class AppEngine {
  JavaVM* jvm_;
  JNIEnv* env_;
  jobject service_instance_;
  jobject status_update_callback_;
  std::shared_ptr<hela::eillance::RtcTransport<hela::eillance::RtcProducer>> rtc_producer;

public:
  explicit AppEngine(JavaVM* jvm, JNIEnv* env, jobject instance, jobject status_update_callback)
      : jvm_(jvm),
        env_(env),
        service_instance_(env->NewWeakGlobalRef(instance)),
        status_update_callback_(env->NewGlobalRef(status_update_callback)) {
    rtc_producer = std::make_shared<hela::eillance::RtcTransport<hela::eillance::RtcProducer>>(StringUtils::RandomString());
    rtc_producer->SetStatusCallback([this](EventSource sender, int status) { setStatus(sender, status); });
    rtc_producer->Start();
  }

  ~AppEngine() {
    env_->DeleteGlobalRef(service_instance_);
    env_->DeleteGlobalRef(status_update_callback_);
  }

  Status GetStatus() {
    return rtc_producer->GetStatus();
  }

  void ConfigureVideo(VideoConfig conf) {
    rtc_producer->ConfigureVideo(std::move(conf));
  }

private:
  JNIEnv* attachCurrentThreadIfNeeded() {
    JNIEnv* env;
    jint res = jvm_->GetEnv((void**)&env, JNI_VERSION_1_6);
    switch (res) {
      case JNI_OK:
        // the current thread is already attached, return the JNIEnv.
        return env;
      case JNI_EDETACHED:
        // the current thread is not attached, try to attach.
        if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          return env;
        } else {
          return nullptr;
        }
      default:
        return nullptr;
    }
  }

  void setStatus(EventSource sender, int status) {
    JNIEnv* env = attachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;
    }
    jclass callbackClass = env->GetObjectClass(status_update_callback_);
    jmethodID callbackMethod = env->GetMethodID(callbackClass, "onStatusUpdate", "(II)V");
    if (callbackMethod != nullptr) {
      env->CallVoidMethod(status_update_callback_, callbackMethod, static_cast<int>(sender), status);
    }
  }

};

}  // namespace hela::eillance
