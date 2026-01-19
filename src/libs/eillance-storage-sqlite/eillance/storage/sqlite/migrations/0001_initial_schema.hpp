#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>

#include "eillance/logging/logging.h"

namespace eillance::storage::migrations {

inline void MigrateToV0001(sqlite3* db) {
  LGI << "Initializing database schema (v1)...";
  const auto schema = R"SQL(
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
        photo_base64          TEXT,
        signature_from_them   TEXT,
        my_signature_for_them TEXT
      );

      CREATE TABLE IF NOT EXISTS messages (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        peer_id          TEXT NOT NULL,
        kind             INTEGER NOT NULL,
        timestamp        INTEGER NOT NULL,
        direction        INTEGER NOT NULL,
        status           INTEGER NOT NULL,
        content          TEXT,
        is_truncated     INTEGER NOT NULL DEFAULT 0
      );

      CREATE TABLE IF NOT EXISTS large_messages (
        message_id   INTEGER PRIMARY KEY NOT NULL,
        full_content TEXT,
        FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE
      );

      CREATE INDEX IF NOT EXISTS idx_messages_peer_timestamp ON messages (peer_id, timestamp);

      PRAGMA user_version = 1;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute initial migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGI << "Database schema initialized.";
}

}  // namespace eillance::storage::migrations
