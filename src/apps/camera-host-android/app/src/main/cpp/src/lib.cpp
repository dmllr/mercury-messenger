#include <cstring>
#include <jni.h>

#include "app_engine.hpp"

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/logging/logging.h"

/// @brief Application engine class alias
using AppEngine = hela::eillance::AppEngine;

namespace {

/// @brief AppEngine object: the top level application engine object
AppEngine* engine = nullptr;

/// @brief Global JVM object
JavaVM* javaVm;

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_AppEngineService_createAppEngine(
    JNIEnv* env,
    jobject instance,
    jobject nativeBridge) {
  engine = new AppEngine(javaVm, env, instance, nativeBridge);
  return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_deleteAppEngine(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  if (!engine || !appmgr) {
    return;
  }
  auto* app = reinterpret_cast<AppEngine*>(appmgr);
  LOGF_IF(app != engine) << "NdkCamera Obj mismatch";

  delete app;

  // also reset the private global object
  engine = nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_AppEngineService_getServiceConnectionState(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  auto* app = reinterpret_cast<AppEngine*>(appmgr);
  auto status = app->GetServiceConnectionState();

  auto* clss = env->FindClass("hela/eillance/events/ServiceConnectionState");
  assert(clss != nullptr && "Class ServiceConnectionState can't be found");
  auto* from_int_method = env->GetStaticMethodID(clss, "fromInt", "(I)Lhela/eillance/events/ServiceConnectionState;");
  assert(from_int_method != nullptr && "Static method ServiceConnectionState.fromInt can't be found");

  // NOLINTNEXTLINE(*-vararg)
  return env->CallStaticObjectMethod(clss, from_int_method, static_cast<jint>(status));
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_configureDeviceName(
    JNIEnv *env,
    jobject instance,
    jlong appmgr,
    jstring deviceName) {
  auto* app = reinterpret_cast<AppEngine*>(appmgr);

  const char* deviceNameCStr = env->GetStringUTFChars(deviceName, nullptr);
  auto device_name = std::string(deviceNameCStr);
  env->ReleaseStringUTFChars(deviceName, deviceNameCStr);

  app->ConfigureDeviceName(std::move(device_name));
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_configureVideoCapture(
    JNIEnv *env,
    jobject instance,
    jobject videoConfig) {
  jclass videoConfigClass = env->GetObjectClass(videoConfig);

  hela::eillance::VideoConfig conf{};

  conf.cameraId = env->GetIntField(videoConfig, env->GetFieldID(videoConfigClass, "cameraId", "I"));
  conf.width = env->GetIntField(videoConfig, env->GetFieldID(videoConfigClass, "width", "I"));
  conf.height = env->GetIntField(videoConfig, env->GetFieldID(videoConfigClass, "height", "I"));
  conf.frameRate = env->GetIntField(videoConfig, env->GetFieldID(videoConfigClass, "frameRate", "I"));
  conf.bitrate = env->GetIntField(videoConfig, env->GetFieldID(videoConfigClass, "bitrate", "I"));

  hela::eillance::EventQueue::Instance().Push(std::move(conf));
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  javaVm = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnLoad(JavaVM* jvm, void* reserved) {
  JNIEnv* env;
  if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
}
