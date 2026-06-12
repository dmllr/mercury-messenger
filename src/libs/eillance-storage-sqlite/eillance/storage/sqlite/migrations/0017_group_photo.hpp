#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0017(sqlite3* db) {
  LGV << "Migrating database schema to v17 (Group Photo)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE groups ADD COLUMN photo_base64 TEXT;

      PRAGMA user_version = 17;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v17 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v17.";
}

}  // namespace eillance::storage::migrations
