#pragma once

#include <jni.h>
#include <string>
#include <unordered_map>
#include <stdexcept>

/// Helper class that holds a global reference to a Java class and caches its methods.
class ClassHolder {
public:
  /// Construct a ClassHolder from a global reference to a @c jclass.
  explicit ClassHolder(jclass clazz = nullptr)
      : class_(clazz), ctor_(nullptr) {}

  /// Return the stored @c jclass.
  [[nodiscard]] jclass Class() const { return class_; }

  /// Set and get the default constructor.
  void SetCtor(jmethodID ctor) { ctor_ = ctor; }

  [[nodiscard]] jmethodID Ctor() const { return ctor_; }

  /// Create a new object using the cached constructor.
  template<typename... Args>
  jobject NewObject(JNIEnv* env, Args... args) const {
    // NOLINTNEXTLINE(*-vararg)
    return env->NewObject(class_, ctor_, args...);
  }

private:
  jclass class_;
  jmethodID ctor_;
};

// Singleton that stores global references to Java classes and their methods.
class ClassRefs {
public:
  // Get the singleton instance.
  static ClassRefs& Instance() {
    static ClassRefs instance;
    return instance;
  }

  // Registers a class by name if it hasn't already been registered.
  // The class name must be in JNI format (e.g. "java/util/ArrayList").
  void RegisterClass(JNIEnv* env, const std::string& className) {
    if (classes_.find(className) == classes_.end()) {
      jclass local_cls = env->FindClass(className.c_str());
      if (local_cls == nullptr) {
        throw std::runtime_error("Failed to find class: " + className);
      }
      // NOLINTNEXTLINE(*-readability-casting)
      auto* global_cls = static_cast<jclass>(env->NewGlobalRef(local_cls));
      classes_[className] = ClassHolder(global_cls);
      env->DeleteLocalRef(local_cls);
    }
  }

  // Registers a constructor for a given class (by class name) with the given signature.
  // Example: registerCtor(env, "java/util/ArrayList", "()V");
  void RegisterCtor(JNIEnv* env, const std::string& className, const std::string& ctorSig) {
    RegisterClass(env, className);
    jclass cls = classes_[className].Class();
    jmethodID ctor = env->GetMethodID(cls, "<init>", ctorSig.c_str());
    if (ctor == nullptr) {
      throw std::runtime_error("Failed to get constructor for: " + className);
    }
    classes_[className].SetCtor(ctor);
  }

  // Retrieve the ClassHolder for a given class.
  ClassHolder& Class(const std::string& className) {
    auto it = classes_.find(className);
    if (it == classes_.end()) {
      throw std::runtime_error("Class not registered: " + className);
    }
    return it->second;
  }

private:
  ClassRefs() = default;
  ClassRefs(ClassRefs&&) = delete;
  ClassRefs(const ClassRefs&) = delete;
  ClassRefs& operator=(ClassRefs&&) = delete;
  ClassRefs& operator=(const ClassRefs&) = delete;
  ~ClassRefs() = default;

  // Map of class name to cached class information.
  std::unordered_map<std::string, ClassHolder> classes_;
};
