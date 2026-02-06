#include <vector>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/view_model/debug_view_model.hpp"
#include "utils/class_refs.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeCreate(JNIEnv* /*env*/, jobject /*instance*/) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::DebugViewModel(g_clientEngine->GetClient(), g_clientEngine->GetRouter());
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeEraseHistory(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->EraseAllHistory();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeEraseContacts(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->EraseAllContacts();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeRevokeApprovals(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->RevokeAllApprovals();
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeGetPendingActions(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  auto actions = vm->GetPendingActions();

  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject j_list = list_holder.NewObject(env);
  jmethodID list_add = list_holder.GetMethod("add");

  auto& item_cls = class_refs.Class("hela/eillance/ndkio/DebugPendingAction");

  for (auto const& action : actions) {
    jstring j_peer = env->NewStringUTF(action.peer_id.ToString().c_str());
    jstring j_payload = env->NewStringUTF(action.payload.c_str());
    jobject j_item = item_cls.NewObject(env, j_peer, (jlong)action.id, j_payload);

    env->CallBooleanMethod(j_list, list_add, j_item);

    env->DeleteLocalRef(j_peer);
    env->DeleteLocalRef(j_payload);
    env->DeleteLocalRef(j_item);
  }
  return j_list;
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeGetConnectionInfo(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  auto infos = vm->GetConnectionInfo();

  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject j_list = list_holder.NewObject(env);
  jmethodID list_add = list_holder.GetMethod("add");

  auto& item_cls = class_refs.Class("hela/eillance/ndkio/DebugConnectionInfo");

  for (auto const& info : infos) {
    jstring j_peer = env->NewStringUTF(info.peer_id.ToString().c_str());
    jobject j_item = item_cls.NewObject(
        env,
        j_peer,
        (jint)info.state,
        (jlong)info.last_activity_sec_ago,
        (jboolean)info.is_data_channel_open,
        (jboolean)info.has_media_connection
    );

    env->CallBooleanMethod(j_list, list_add, j_item);
    env->DeleteLocalRef(j_peer);
    env->DeleteLocalRef(j_item);
  }
  return j_list;
}

extern "C" JNIEXPORT jobject JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeGetUnreadInfo(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  auto infos = vm->GetUnreadInfo();

  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject j_list = list_holder.NewObject(env);
  jmethodID list_add = list_holder.GetMethod("add");

  auto& item_cls = class_refs.Class("hela/eillance/ndkio/DebugUnreadInfo");

  for (auto const& info : infos) {
    jstring j_peer = env->NewStringUTF(info.peer_id.ToString().c_str());
    jobject j_item = item_cls.NewObject(env, j_peer, (jint)info.count, (jlong)info.last_msg_timestamp);

    env->CallBooleanMethod(j_list, list_add, j_item);
    env->DeleteLocalRef(j_peer);
    env->DeleteLocalRef(j_item);
  }
  return j_list;
}

// NOLINTEND(readability-identifier-naming)
