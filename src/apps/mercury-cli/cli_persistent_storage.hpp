#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::storage {

// Filesystem-based implementation of the storage interface for the CLI client.
class CliPersistentStorageImpl final : public PersistentStorageImpl {
  std::filesystem::path root_path_;

public:
  explicit CliPersistentStorageImpl(std::filesystem::path root_path) : root_path_(std::move(root_path)) {
    if (!std::filesystem::exists(root_path_)) {
      std::filesystem::create_directories(root_path_);
    }
  }

  auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool override {
    try {
      std::ofstream file(root_path_ / key, std::ios::binary | std::ios::trunc);
      if (!file.is_open()) {
        LOGE << "Failed to open file for writing: " << (root_path_ / key).string();
        return false;
      }
      file.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
      return file.good();
    } catch (const std::exception& e) {
      LOGE << "Exception while saving to file: " << e.what();
      return false;
    }
  }

  auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> override {
    try {
      std::ifstream file(root_path_ / key, std::ios::binary | std::ios::ate);
      if (!file.is_open()) { return std::nullopt; }
      const std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      std::vector<std::byte> buffer(static_cast<size_t>(size));
      if (file.read(reinterpret_cast<char*>(buffer.data()), size)) { return buffer; }
    } catch (const std::exception& e) {
      LOGE << "Exception while loading from file: " << e.what();
    }
    return std::nullopt;
  }

  auto Exists(const std::string& key) -> bool override { return std::filesystem::exists(root_path_ / key); }

  auto Delete(const std::string& key) -> bool override {
    try {
      return std::filesystem::remove(root_path_ / key);
    } catch (const std::filesystem::filesystem_error& e) {
      LOGE << "Error deleting file: " << e.what();
      return false;
    }
  }
};

}  // namespace eillance::storage
