#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>

#include <jni.h>

/// Helper class that holds a global reference to a Java class and caches its methods.
class ClassHolder {
 public:
  /// Construct a ClassHolder from a global reference to a @c jclass.
  explicit ClassHolder(jclass clazz)
      : class_(clazz),
        ctor_(nullptr) {}

  /// Return the stored @c jclass.
  [[nodiscard]] [[maybe_unused]] jclass Class() const { return class_; }

  /// Set the default constructor.
  void SetCtor(jmethodID ctor) { ctor_ = ctor; }

  /// Get the default constructor.
  [[nodiscard]] [[maybe_unused]] jmethodID Ctor() const { return ctor_; }

  /// Cache a method ID.
  void AddMethod(std::string const& method_name, jmethodID method_id) { methods_[method_name] = method_id; }

  /// Get a cached method ID.
  [[nodiscard]] jmethodID GetMethod(std::string const& method_name) const {
    auto it = methods_.find(method_name);
    if (it == methods_.end()) {
      throw std::runtime_error("Method not registered: " + method_name);
    }
    return it->second;
  }

  /// Cache a field ID.
  void AddField(std::string const& field_name, jfieldID field_id) { fields_[field_name] = field_id; }

  /// Get a cached field ID.
  [[nodiscard]] jfieldID GetField(std::string const& field_name) const {
    auto it = fields_.find(field_name);
    if (it == fields_.end()) {
      throw std::runtime_error("Field not registered: " + field_name);
    }
    return it->second;
  }
  /// Create a new object using the cached constructor.
  template <typename... Args>
  [[nodiscard]] [[maybe_unused]] jobject NewObject(JNIEnv* env, Args... args) const {
    // NOLINTNEXTLINE(*-vararg)
    return env->NewObject(class_, ctor_, args...);
  }

 private:
  jclass class_;
  jmethodID ctor_;
  std::unordered_map<std::string, jfieldID> fields_;
  std::unordered_map<std::string, jmethodID> methods_;
};

/// Singleton that stores global references to Java classes and their methods.
class ClassRefs {
  ClassRefs() = default;

  /// Map of class name to cached class information.
  std::unordered_map<std::string, ClassHolder> classes_;

 public:
  ClassRefs(ClassRefs&&) = delete;
  ClassRefs(ClassRefs const&) = delete;
  auto operator=(ClassRefs&&) -> ClassRefs& = delete;
  auto operator=(ClassRefs const&) -> ClassRefs& = delete;
  ~ClassRefs() = default;

  /// Get the singleton instance.
  static auto Instance() -> ClassRefs& {
    static ClassRefs instance;
    return instance;
  }

  /// Registers a class by name if it hasn't already been registered.
  /// The class name must be in JNI format (e.g. "java/util/ArrayList").
  void RegisterClass(JNIEnv* env, std::string const& class_name) {
    if (classes_.find(class_name) == classes_.end()) {
      jclass local_cls = env->FindClass(class_name.c_str());
      if (local_cls == nullptr) {
        throw std::runtime_error("Failed to find class: " + class_name);
      }
      // NOLINTNEXTLINE(*-readability-casting, *-pro-type-static-cast-downcast)
      auto* global_cls = static_cast<jclass>(env->NewGlobalRef(local_cls));
      classes_.emplace(class_name, ClassHolder(global_cls));
      env->DeleteLocalRef(local_cls);
    }
  }

  /// Registers a constructor for a given class (by class name) with the given signature.
  /// Example: registerCtor(env, "java/util/ArrayList", "()V");
  void RegisterCtor(JNIEnv* env, std::string const& class_name, std::string const& ctor_sig) {
    RegisterClass(env, class_name);
    jclass cls = classes_.at(class_name).Class();
    jmethodID ctor = env->GetMethodID(cls, "<init>", ctor_sig.c_str());
    if (ctor == nullptr) {
      throw std::runtime_error("Failed to get constructor for: " + class_name);
    }
    classes_.at(class_name).SetCtor(ctor);
  }

  /// Registers a method for a given class with the given name and signature.
  void RegisterMethod(
      JNIEnv* env,
      std::string const& class_name,
      std::string const& method_name,
      std::string const& method_sig
  ) {
    RegisterClass(env, class_name);
    jclass cls = classes_.at(class_name).Class();
    jmethodID mid = env->GetMethodID(cls, method_name.c_str(), method_sig.c_str());
    if (mid == nullptr) {
      throw std::runtime_error("Failed to get method '" + method_name + "' for class: " + class_name);
    }
    classes_.at(class_name).AddMethod(method_name, mid);
  }

  /// Registers a static method for a given class with the given name and signature.
  void RegisterStaticMethod(
      JNIEnv* env,
      std::string const& class_name,
      std::string const& method_name,
      std::string const& method_sig
  ) {
    RegisterClass(env, class_name);
    jclass cls = classes_.at(class_name).Class();
    jmethodID mid = env->GetStaticMethodID(cls, method_name.c_str(), method_sig.c_str());
    if (mid == nullptr) {
      throw std::runtime_error("Failed to get static method '" + method_name + "' for class: " + class_name);
    }
    classes_.at(class_name).AddMethod(method_name, mid);
  }

  /// Registers a field for a given class with the given name and signature.
  void RegisterField(
      JNIEnv* env,
      std::string const& class_name,
      std::string const& field_name,
      std::string const& field_sig
  ) {
    RegisterClass(env, class_name);
    jclass cls = classes_.at(class_name).Class();
    jfieldID fid = env->GetFieldID(cls, field_name.c_str(), field_sig.c_str());
    if (fid == nullptr) {
      throw std::runtime_error("Failed to get field '" + field_name + "' for class: " + class_name);
    }
    classes_.at(class_name).AddField(field_name, fid);
  }

  /// Retrieve the ClassHolder for a given class.
  auto Class(std::string const& class_name) -> ClassHolder& {
    auto itr = classes_.find(class_name);
    if (itr == classes_.end()) {
      throw std::runtime_error("Class not registered: " + class_name);
    }
    return itr->second;
  }
};
