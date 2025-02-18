#include <jni.h>
#include <string>

#include "class_refs.hpp"
#include "viewer_engine.hpp"

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

std::unique_ptr<hela::eillance::ViewerEngine> observer{};

}  // namespace

/// @brief JNI function to initialize the observer engine.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewer_ViewerActivity_initEngine(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject native_bridge) {
  namespace he = hela::eillance;
  observer = std::make_unique<he::ViewerEngine>(javaVm, env);
  observer->SetNativeBridge(env, native_bridge);
  observer->Start();
}

/// @brief JNI function to initialize the decoder with a Surface from UI.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewer_ViewerActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  observer->SetTargetSurface(env, surface);
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewer_ViewerActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  observer->StopStreaming();
}

/// @brief JNI function to initiate a stream request.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewer_ViewerActivity_sendStreamRequest(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jstring client_id, jobject jconfig) {
  assert((client_id != nullptr) && (jconfig != nullptr));
  const char* chars = env->GetStringUTFChars(client_id, nullptr);
  const std::string client_id_str(chars);
  env->ReleaseStringUTFChars(client_id, chars);

  // Config class
  jclass config_class = env->GetObjectClass(jconfig);

  // VideoConfig field
  jfieldID video_field = env->GetFieldID(config_class, "video", "Lhela/eillance/ndkio/VideoConfig;");
  jobject jvideo_config = env->GetObjectField(jconfig, video_field);

  // AudioConfig field
  jfieldID audio_field = env->GetFieldID(config_class, "audio", "Lhela/eillance/ndkio/AudioConfig;");
  jobject jaudio_config = env->GetObjectField(jconfig, audio_field);

  hela::eillance::Config config;

  // populate VideoConfig
  if (jvideo_config != nullptr) {
    jclass video_config_class = env->GetObjectClass(jvideo_config);
    jfieldID camera_id_field = env->GetFieldID(video_config_class, "cameraId", "I");
    jfieldID width_field = env->GetFieldID(video_config_class, "width", "I");
    jfieldID height_field = env->GetFieldID(video_config_class, "height", "I");
    jfieldID frame_rate_field = env->GetFieldID(video_config_class, "frameRate", "I");
    jfieldID bitrate_field = env->GetFieldID(video_config_class, "bitrate", "I");

    config.video_config.cameraId = env->GetIntField(jvideo_config, camera_id_field);
    config.video_config.width = env->GetIntField(jvideo_config, width_field);
    config.video_config.height = env->GetIntField(jvideo_config, height_field);
    config.video_config.frameRate = env->GetIntField(jvideo_config, frame_rate_field);
    config.video_config.bitrate = env->GetIntField(jvideo_config, bitrate_field);

    env->DeleteLocalRef(video_config_class);
  }

  // populate AudioConfig
  if (jaudio_config != nullptr) {
    // so far audio is not configurable, skip everything, but keep commented
  }

  env->DeleteLocalRef(config_class);
  env->DeleteLocalRef(jvideo_config);
  env->DeleteLocalRef(jaudio_config);

  observer->SendStreamRequest(client_id_str, config);
}

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;

  JNIEnv* env = nullptr;
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  try {
    auto& classref = ClassRefs::Instance();
    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Device", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Camera", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Resolution", "(II)V");
  } catch (const std::exception& e) {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
