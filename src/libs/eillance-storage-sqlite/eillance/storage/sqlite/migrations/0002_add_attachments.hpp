#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0002(sqlite3* db) {
  LGV << "Migrating database schema to v2 (Media Attachments)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS message_attachments (
        id               TEXT PRIMARY KEY NOT NULL,
        peer_id          TEXT NOT NULL,
        message_id       INTEGER NOT NULL,
        type             INTEGER NOT NULL,
        file_path        TEXT NOT NULL,
        mime_type        TEXT NOT NULL,
        size_bytes       INTEGER NOT NULL,
        thumbnail_base64 TEXT,
        hash             TEXT,
        is_downloaded    INTEGER NOT NULL DEFAULT 0,
        FOREIGN KEY(peer_id, message_id) REFERENCES messages(peer_id, id) ON DELETE CASCADE
      );

      PRAGMA user_version = 2;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v2 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v2.";
}

}  // namespace eillance::storage::migrations
