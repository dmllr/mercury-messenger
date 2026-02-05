#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0007(sqlite3* db) {
  LGI << "Migrating database schema to v7 (Pending Actions V2)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE pending_actions ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0;
      ALTER TABLE pending_actions ADD COLUMN ref_id INTEGER;

      CREATE INDEX IF NOT EXISTS idx_pending_actions_peer_ref ON pending_actions (peer_id, ref_id);

      PRAGMA user_version = 7;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v7 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGI << "Database schema updated to v7.";
}

}  // namespace eillance::storage::migrations
