#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "eillance/storage/persistent_storage.hpp"

namespace eillance::storage {

class MockPersistentStorageImpl final : public PersistentStorageImpl {
  std::unordered_map<std::string, std::vector<std::byte>> storage_;

public:
  auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool override {
    storage_[key] = value;
    return true;
  }

  auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> override {
    if (const auto it = storage_.find(key); it != storage_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  auto Exists(const std::string& key) -> bool override { return storage_.contains(key); }

  auto Delete(const std::string& key) -> bool override { return storage_.erase(key) > 0; }

  void Clear() { storage_.clear(); }
};

}  // namespace eillance::storage
