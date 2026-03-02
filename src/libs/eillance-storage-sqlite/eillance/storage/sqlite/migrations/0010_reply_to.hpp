#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0010(sqlite3* db) {
  LGV << "Migrating database schema to v10 (Reply To)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;
      ALTER TABLE messages ADD COLUMN reply_to_id INTEGER DEFAULT 0;
      PRAGMA user_version = 10;
      COMMIT;
    )SQL";
  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v10 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v10.";
}

}  // namespace eillance::storage::migrations
