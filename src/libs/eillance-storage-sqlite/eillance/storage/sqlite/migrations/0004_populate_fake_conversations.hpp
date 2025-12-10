#pragma once

#include <sqlite3.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/sqlite_helpers.hpp"

namespace eillance::storage::migrations {

inline void migrate_to_v4(sqlite3* db) {
  LOGI << "Migrating to version 4...";
  if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to begin V4 migration transaction.");
  }

  try {
    if (sqlite3_exec(db, "PRAGMA user_version = 4;", nullptr, nullptr, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to update user_version to 4.");
    }

  } catch (const std::exception& e) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    LOGE << "Migration to V4 failed: " << e.what();
    throw;
  }

  if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to commit V4 migration transaction.");
  }
  LOGI << "Database migrated to version 4.";
}

}  // namespace eillance::storage::migrations
