#include <jni.h>
#include <string>
#include <cassert>

#include "app_engine.hpp"
#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/logging/logging.h"

using AppEngine = hela::eillance::AppEngine;

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_AppEngineService_createAppEngine(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject instance, jobject nativeBridge) {
  return reinterpret_cast<jlong>(new AppEngine(javaVm, env, instance, nativeBridge));  // NOLINT(*-pro-type-reinterpret-cast)
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_deleteAppEngine(  // NOLINT(readability-identifier-naming)
    JNIEnv* /*env*/, jobject /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<AppEngine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  delete app;
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_start(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<AppEngine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  app->Start();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_stop(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<AppEngine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  app->Stop();
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_AppEngineService_getServiceConnectionState(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<AppEngine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  auto status = static_cast<int>(app->GetServiceConnectionState());

  jclass cls = env->FindClass("hela/eillance/events/ServiceConnectionState");
  assert(cls != nullptr && "Class ServiceConnectionState can't be found");

  jmethodID from_int_method = env->GetStaticMethodID(cls, "fromInt", "(I)Lhela/eillance/events/ServiceConnectionState;");
  assert(from_int_method != nullptr && "Static method ServiceConnectionState.fromInt can't be found");

  // NOLINTNEXTLINE(*-vararg)
  return env->CallStaticObjectMethod(cls, from_int_method, status);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_configureDeviceName(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong appmgr, jstring deviceName) {

  auto* app = reinterpret_cast<AppEngine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  auto* device_name_c_str = env->GetStringUTFChars(deviceName, nullptr);
  auto device_name_str = std::string(device_name_c_str);
  env->ReleaseStringUTFChars(deviceName, device_name_c_str);

  app->ConfigureDeviceName(device_name_str);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_configureVideoCapture(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong appmgr, jobject videoConfig) {

  auto conf = hela::eillance::VideoConfig{};
  jclass video_config_class = env->GetObjectClass(videoConfig);
  conf.cameraId = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "cameraId", "I"));
  conf.width = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "width", "I"));
  conf.height = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "height", "I"));
  conf.frameRate = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "frameRate", "I"));
  conf.bitrate = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "bitrate", "I"));

  hela::eillance::EventQueue::Instance().Push(std::move(conf));
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;
  return JNI_VERSION_1_6;
}
