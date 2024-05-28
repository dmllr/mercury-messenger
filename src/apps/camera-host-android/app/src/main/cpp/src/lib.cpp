#include <cstring>
#include <jni.h>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/logging/logging.h"


#include "app_engine.hpp"


/**
 * AppEngine object:
 *   the top level application engine object
 */
using AppEngine = hela::eillance::AppEngine;
AppEngine* engine = nullptr;

JavaVM* g_JavaVM;

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_AppEngineService_createAppEngine(
    JNIEnv* env,
    jobject instance,
    jobject statusUpdateCallback) {
  engine = new AppEngine(g_JavaVM, env, instance, statusUpdateCallback);
  return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_deleteAppEngine(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  if (!engine || !appmgr)
    return;
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  LOGF_IF(app != engine) << "NdkCamera Obj mismatch";

  delete app;

  // also reset the private global object
  engine = nullptr;
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_AppEngineService_getStatus(
    JNIEnv* env,
    jobject instance,
    jlong appmgr) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);
  auto status = app->GetStatus();

  jclass statusClass = env->FindClass("hela/eillance/ndkio/Status");
  jmethodID fromIntMethod = env->GetStaticMethodID(statusClass, "fromInt", "(IIIII)Lhela/eillance/ndkio/Status;");
  assert(fromIntMethod != nullptr && "Static method Status.fromInt can't be found");

  return env->CallStaticObjectMethod(statusClass, fromIntMethod,
                                                  static_cast<jint>(status.service_state),
                                                  static_cast<jint>(status.state),
                                                  static_cast<jint>(status.ice_state),
                                                  static_cast<jint>(status.gathering_state),
                                                  static_cast<jint>(status.signaling_state));
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_AppEngineService_configureDeviceName(
    JNIEnv *env,
    jobject instance,
    jlong appmgr,
    jstring deviceName) {
  auto app = reinterpret_cast<AppEngine*>(appmgr);

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
  g_JavaVM = vm;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnLoad(JavaVM* jvm, void* reserved) {
  JNIEnv* env;
  if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return;
  }
}
