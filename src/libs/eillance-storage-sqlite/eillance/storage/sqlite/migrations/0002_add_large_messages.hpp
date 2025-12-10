#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>

#include "eillance/logging/logging.h"

namespace eillance::storage::migrations {

inline void migrate_to_v2(sqlite3* db) {
  LOGI << "Database version is 1, migrating to version 2...";
  const char* schema = R"SQL(
      BEGIN TRANSACTION;
      ALTER TABLE history ADD COLUMN is_truncated INTEGER NOT NULL DEFAULT 0;
      CREATE TABLE large_messages (
        history_id   INTEGER PRIMARY KEY NOT NULL,
        full_content TEXT,
        FOREIGN KEY(history_id) REFERENCES history(id) ON DELETE CASCADE
      );
      PRAGMA user_version = 2;
      COMMIT;
    )SQL";
  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute migration to V2: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LOGI << "Database migrated to version 2: Added support for large messages.";
}

}  // namespace eillance::storage::migrations
