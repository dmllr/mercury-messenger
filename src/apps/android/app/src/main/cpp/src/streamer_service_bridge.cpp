#include <jni.h>
#include <string>
#include <cassert>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/logging/logging.h"

#include "streamer_engine.hpp"

using Engine = hela::eillance::StreamerEngine;

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_streamer_StreamerService_createAppEngine(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject instance, jobject nativeBridge) {
  return reinterpret_cast<jlong>(new Engine(javaVm, env, instance, nativeBridge));  // NOLINT(*-pro-type-reinterpret-cast)
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_deleteAppEngine(  // NOLINT(readability-identifier-naming)
    JNIEnv*, jobject /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  delete app;
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_start(  // NOLINT(readability-identifier-naming)
    JNIEnv*, jobject  /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  app->Start();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_stop(  // NOLINT(readability-identifier-naming)
    JNIEnv*, jobject  /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  app->Stop();
}

/// @brief JNI function to update a user token.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_updateAuthToken(  // NOLINT(*-identifier-naming)
        JNIEnv* env, jobject, jlong appmgr, jstring jtoken) {
    auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
    const char* chars = env->GetStringUTFChars(jtoken, nullptr);
    const std::string token(chars);
    env->ReleaseStringUTFChars(jtoken, chars);
    app->UpdateAuthToken(token);
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_streamer_StreamerService_getServiceConnectionState(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject /*instance*/, jlong appmgr) {

  auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  auto status = static_cast<int>(app->GetServiceConnectionState());

  jclass cls = env->FindClass("hela/eillance/ndkio/ServiceConnectionState");
  assert(cls != nullptr && "Class ServiceConnectionState can't be found");

  jmethodID from_int_method = env->GetStaticMethodID(cls, "fromInt", "(I)Lhela/eillance/ndkio/ServiceConnectionState;");
  assert(from_int_method != nullptr && "Static method ServiceConnectionState.fromInt can't be found");

  // NOLINTNEXTLINE(*-vararg)
  return env->CallStaticObjectMethod(cls, from_int_method, status);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_configureDeviceName(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong appmgr, jstring deviceName) {

  auto* app = reinterpret_cast<Engine*>(appmgr); // NOLINT(*-pro-type-reinterpret-cast, *-no-int-to-ptr)
  auto* device_name_c_str = env->GetStringUTFChars(deviceName, nullptr);
  auto device_name_str = std::string(device_name_c_str);
  env->ReleaseStringUTFChars(deviceName, device_name_c_str);

  app->ConfigureDeviceName(device_name_str);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_streamer_StreamerService_configureVideoCapture(  // NOLINT(readability-identifier-naming)
    JNIEnv* env, jobject  /*instance*/, jlong /*appmgr*/, jobject videoConfig) {

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
