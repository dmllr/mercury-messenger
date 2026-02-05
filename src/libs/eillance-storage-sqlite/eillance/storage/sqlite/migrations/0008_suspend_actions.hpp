#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0008(sqlite3* db) {
  LGI << "Migrating database schema to v8 (Suspension Logic)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE pending_actions ADD COLUMN is_suspended INTEGER DEFAULT 0;
      ALTER TABLE pending_actions ADD COLUMN window_start_at INTEGER DEFAULT 0;

      -- Backfill window_start_at with created_at for existing actions
      UPDATE pending_actions SET window_start_at = created_at;

      PRAGMA user_version = 8;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v8 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGI << "Database schema updated to v8.";
}

}  // namespace eillance::storage::migrations
