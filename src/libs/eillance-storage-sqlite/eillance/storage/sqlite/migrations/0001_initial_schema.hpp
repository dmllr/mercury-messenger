#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>

#include "eillance/logging/logging.h"

namespace eillance::storage::migrations {

inline void migrate_to_v1(sqlite3* db) {
  LOGI << "Database version is 0, migrating to version 1...";
  const char* schema = R"SQL(
      BEGIN TRANSACTION;
      CREATE TABLE IF NOT EXISTS settings (
        key   TEXT PRIMARY KEY NOT NULL,
        value TEXT
      );
      CREATE TABLE IF NOT EXISTS contacts (
        peer_id               TEXT PRIMARY KEY NOT NULL,
        status                INTEGER NOT NULL,
        display_name          TEXT,
        display_name_override TEXT,
        bio                   TEXT,
        photo_base64          TEXT
      );
      CREATE TABLE IF NOT EXISTS history (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        peer_id          TEXT NOT NULL,
        kind             INTEGER NOT NULL,
        timestamp        INTEGER NOT NULL,
        direction        INTEGER NOT NULL,
        status           INTEGER NOT NULL,
        content          TEXT,
        duration_seconds INTEGER,
        call_direction   INTEGER,
        call_status      INTEGER
      );
      CREATE INDEX IF NOT EXISTS idx_history_peer_timestamp ON history (peer_id, timestamp);
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute migration to V1: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LOGI << "Database migrated to version 1: Foundation.";
}

}  // namespace eillance::storage::migrations
