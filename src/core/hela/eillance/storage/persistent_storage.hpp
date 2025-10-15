#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <filesystem>

// Forward declarations for Android JNI types
struct _JNIEnv;
struct _JavaVM;
typedef _JavaVM JavaVM;
typedef struct _jobject* jobject;


namespace hela::eillance::storage {

// A configuration variant for different platforms.
struct Config {
#ifdef __ANDROID__
  struct Android {
    JavaVM* jvm;
    jobject context;
  };
  std::variant<std::filesystem::path, Android> platform_config;
#else
  std::variant<std::filesystem::path> platform_config;
#endif
};

class PersistentStorage {
public:
  explicit PersistentStorage(const Config& config);
  ~PersistentStorage();
  PersistentStorage(const PersistentStorage&) = delete;
  PersistentStorage& operator=(const PersistentStorage&) = delete;
  PersistentStorage(PersistentStorage&&) noexcept;
  PersistentStorage& operator=(PersistentStorage&&) noexcept;

  auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool;
  auto Load(const std::string& key) -> std::optional<std::vector<std::byte>>;
  auto Exists(const std::string& key) -> bool;
  auto Delete(const std::string& key) -> bool;

private:
  class Impl;
  std::unique_ptr<Impl> pimpl_;
};

}  // namespace hela::eillance::storage
