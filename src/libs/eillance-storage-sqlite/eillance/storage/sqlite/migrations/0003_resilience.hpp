#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0003(sqlite3* db) {
  LGI << "Migrating database schema to v3 (Resilience)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS pending_read_receipts (
        peer_id    TEXT NOT NULL,
        message_id INTEGER NOT NULL,
        PRIMARY KEY (peer_id, message_id)
      );

      ALTER TABLE contacts ADD COLUMN profile_sync_needed INTEGER DEFAULT 0;

      PRAGMA user_version = 3;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v3 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGI << "Database schema updated to v3.";
}

}  // namespace eillance::storage::migrations
