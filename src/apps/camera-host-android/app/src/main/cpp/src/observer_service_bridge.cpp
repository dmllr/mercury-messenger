#include "class_refs.hpp"
#include "observer_engine.hpp"

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

std::unique_ptr<hela::eillance::ObserverEngine> observer{};

}  // namespace

/// @brief JNI function to initialize the observer engine.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initEngine(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject native_bridge) {
  namespace he = hela::eillance;
  observer = std::make_unique<he::ObserverEngine>(javaVm, env);
  observer->SetNativeBridge(env, native_bridge);
  observer->Start();
}

/// @brief JNI function to initialize the decoder with a Surface from UI.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  observer->SetTargetSurface(env, surface);
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  observer->Stop();
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
    classref.RegisterCtor(env, "hela/eillance/ndkio/ClientInfo", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Camera", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Resolution", "(II)V");
  } catch (const std::exception& e) {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
