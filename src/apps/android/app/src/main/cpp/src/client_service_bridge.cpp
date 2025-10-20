#include <jni.h>
#include <cassert>
#include <memory>
#include <string>

#include "class_refs.hpp"
#include "client_engine.hpp"

namespace {
JavaVM* javaVm = nullptr;
std::unique_ptr<eillance::ClientEngine> clientEngine{};
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_initEngine(JNIEnv* env, jobject, jobject native_bridge) {
  if (clientEngine == nullptr) {
    clientEngine = std::make_unique<eillance::ClientEngine>(javaVm, env, native_bridge);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_start(JNIEnv*, jobject) {
  if (clientEngine) clientEngine->Start();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_terminate(JNIEnv*, jobject) {
  if (clientEngine) {
    clientEngine->Terminate();
    clientEngine.reset();
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_initDecoder(JNIEnv* env, jobject, jobject surface) {
  if (clientEngine) clientEngine->SetTargetSurface(env, surface);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_releaseDecoder(JNIEnv*, jobject) {
  if (clientEngine) clientEngine->Stop();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_requestPeerListUpdate(JNIEnv*, jobject) {
  if (clientEngine) clientEngine->RequestPeerListUpdate();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_call(JNIEnv* env, jobject, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (clientEngine) clientEngine->Call(peer_id_str);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_addContact(JNIEnv* env, jobject, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (clientEngine) clientEngine->AddContact(peer_id_str);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_configureVideoCapture(JNIEnv* env, jobject, jobject videoConfig) {
  if (clientEngine) {
    auto conf = eillance::VideoConfig{};
    jclass video_config_class = env->GetObjectClass(videoConfig);
    conf.cameraId = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "cameraId", "I"));
    conf.width = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "width", "I"));
    conf.height = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "height", "I"));
    conf.frameRate = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "frameRate", "I"));
    conf.bitrate = env->GetIntField(videoConfig, env->GetFieldID(video_config_class, "bitrate", "I"));
    clientEngine->ConfigureVideoCapture(conf);
  }
}

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  Log::InitializeDefault(plog::info);

  try {
    auto& classref = ClassRefs::Instance();
    classref.RegisterClass(env, "androidx/preference/PreferenceManager");
    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Peer", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Camera", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Resolution", "(II)V");
  } catch (const std::exception& e) {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
