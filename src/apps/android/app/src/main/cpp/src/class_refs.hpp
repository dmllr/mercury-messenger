#pragma once

#include <jni.h>
#include <stdexcept>
#include <string>
#include <unordered_map>

/// Helper class that holds a global reference to a Java class and caches its methods.
class ClassHolder {
public:
  /// Construct a ClassHolder from a global reference to a @c jclass.
  explicit ClassHolder(jclass clazz)
      : class_(clazz), ctor_(nullptr) {}

  /// Return the stored @c jclass.
  [[nodiscard]] [[maybe_unused]]
  jclass Class() const { return class_; }

  /// Set the default constructor.
  void SetCtor(jmethodID ctor) { ctor_ = ctor; }

  /// Get the default constructor.
  [[nodiscard]] [[maybe_unused]]
  jmethodID Ctor() const { return ctor_; }

  /// Create a new object using the cached constructor.
  template<typename... Args>
  [[nodiscard]] [[maybe_unused]]
  jobject NewObject(JNIEnv* env, Args... args) const {
    // NOLINTNEXTLINE(*-vararg)
    return env->NewObject(class_, ctor_, args...);
  }

private:
  jclass class_;
  jmethodID ctor_;
};

/// Singleton that stores global references to Java classes and their methods.
class ClassRefs {
  ClassRefs() = default;

  /// Map of class name to cached class information.
  std::unordered_map<std::string, ClassHolder> classes_;

public:
  ClassRefs(ClassRefs&&) = delete;
  ClassRefs(const ClassRefs&) = delete;
  ClassRefs& operator=(ClassRefs&&) = delete;
  ClassRefs& operator=(const ClassRefs&) = delete;
  ~ClassRefs() = default;

  /// Get the singleton instance.
  static ClassRefs& Instance() {
    static ClassRefs instance;
    return instance;
  }

  /// Registers a class by name if it hasn't already been registered.
  /// The class name must be in JNI format (e.g. "java/util/ArrayList").
  void RegisterClass(JNIEnv* env, const std::string& className) {
    if (classes_.find(className) == classes_.end()) {
      jclass local_cls = env->FindClass(className.c_str());
      if (local_cls == nullptr) {
        throw std::runtime_error("Failed to find class: " + className);
      }
      // NOLINTNEXTLINE(*-readability-casting, *-pro-type-static-cast-downcast)
      auto* global_cls = static_cast<jclass>(env->NewGlobalRef(local_cls));
      classes_.emplace(className, ClassHolder(global_cls));
      env->DeleteLocalRef(local_cls);
    }
  }

  /// Registers a constructor for a given class (by class name) with the given signature.
  /// Example: registerCtor(env, "java/util/ArrayList", "()V");
  void RegisterCtor(JNIEnv* env, const std::string& className, const std::string& ctorSig) {
    RegisterClass(env, className);
    jclass cls = classes_.at(className).Class();
    jmethodID ctor = env->GetMethodID(cls, "<init>", ctorSig.c_str());
    if (ctor == nullptr) {
      throw std::runtime_error("Failed to get constructor for: " + className);
    }
    classes_.at(className).SetCtor(ctor);
  }

  /// Retrieve the ClassHolder for a given class.
  ClassHolder& Class(const std::string& className) {
    auto itr = classes_.find(className);
    if (itr == classes_.end()) {
      throw std::runtime_error("Class not registered: " + className);
    }
    return itr->second;
  }
};
