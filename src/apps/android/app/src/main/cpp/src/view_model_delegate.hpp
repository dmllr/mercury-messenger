#pragma once

#include <jni.h>
#include <memory>
#include <string>
#include <vector>

#include "class_refs.hpp"
#include "eillance/view_model/mercury_view_model.hpp"
#include "utils/jni_utils.hpp"

namespace eillance {

/// Acts as a bridge between the C++ MercuryViewModel and the Android/JNI layer.
/// It owns the ViewModel and exposes its state via query methods, notifying the UI
/// of changes and handling the C++ to Java object conversions.
class AndroidViewModelDelegate {
public:
  AndroidViewModelDelegate(JavaVM* jvm, jobject native_bridge, std::unique_ptr<view_model::MercuryViewModel> view_model);

  AndroidViewModelDelegate(const AndroidViewModelDelegate&) = delete;
  auto operator=(const AndroidViewModelDelegate&) -> AndroidViewModelDelegate& = delete;
  AndroidViewModelDelegate(AndroidViewModelDelegate&&) = delete;
  auto operator=(AndroidViewModelDelegate&&) -> AndroidViewModelDelegate& = delete;
  ~AndroidViewModelDelegate();

  /// Provides direct access to the underlying ViewModel for command methods (e.g., sending a message).
  [[nodiscard]] auto ViewModel() const -> view_model::MercuryViewModel* { return view_model_.get(); }

  // --- State Query Methods ---
  [[nodiscard]] auto GetMyProfile(JNIEnv* env) const -> jobject;
  [[nodiscard]] auto GetContacts(JNIEnv* env) const -> jobject;
  [[nodiscard]] auto GetInteractionHistoryPage(JNIEnv* env) const -> jobject;
  [[nodiscard]] auto GetCallState(JNIEnv* env) const -> jobject;

private:
  void NotifyStateChanged(const std::vector<view_model::StateChange>& changes);

  /// Attaches the current thread to the JVM if necessary, returning a JNIEnv pointer.
  [[nodiscard]] auto AttachCurrentThread() const -> JNIEnv* { return jni::AttachCurrentThreadIfNeeded(jvm_); }

  JavaVM* jvm_;
  jobject native_bridge_{nullptr};
  std::unique_ptr<view_model::MercuryViewModel> view_model_;
};

}  // namespace eillance
