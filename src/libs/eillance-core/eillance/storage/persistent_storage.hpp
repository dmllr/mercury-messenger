#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eillance::storage {

// The abstract interface for platform-specific storage implementations.
// Core depends on this, but implementations will live in platform-specific projects.
class PersistentStorageImpl {
public:
  virtual ~PersistentStorageImpl() = default;
  virtual auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool = 0;
  virtual auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> = 0;
  virtual auto Exists(const std::string& key) -> bool = 0;
  virtual auto Delete(const std::string& key) -> bool = 0;
};


// Public-facing class that uses the pimpl idiom.
// This wrapper is instantiated in platform-specific code (which provides the impl)
// and then passed to core components that require storage access.
class PersistentStorage {
public:
  // The constructor now accepts a pre-constructed, platform-specific implementation.
  explicit PersistentStorage(std::unique_ptr<PersistentStorageImpl> pimpl) : pimpl_(std::move(pimpl)) {}

  ~PersistentStorage() = default;
  PersistentStorage(const PersistentStorage&) = delete;
  auto operator=(const PersistentStorage&) -> PersistentStorage& = delete;
  PersistentStorage(PersistentStorage&&) noexcept = default;
  auto operator=(PersistentStorage&&) noexcept -> PersistentStorage& = default;

  auto Save(const std::string& key, const std::vector<std::byte>& value) const -> bool { return pimpl_->Save(key, value); }
  auto Load(const std::string& key) const -> std::optional<std::vector<std::byte>> { return pimpl_->Load(key); }
  [[nodiscard]] auto Exists(const std::string& key) const -> bool { return pimpl_->Exists(key); }
  auto Delete(const std::string& key) const -> bool { return pimpl_->Delete(key); }

private:
  std::unique_ptr<PersistentStorageImpl> pimpl_;
};

}  // namespace eillance::storage
