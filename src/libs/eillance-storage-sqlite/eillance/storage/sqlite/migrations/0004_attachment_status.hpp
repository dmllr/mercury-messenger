#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0004(sqlite3* db) {
  LGV << "Migrating database schema to v4 (Attachment Status)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE message_attachments ADD COLUMN status INTEGER DEFAULT 0;

      -- Map existing is_downloaded (1) to Done (2)
      UPDATE message_attachments SET status = 2 WHERE is_downloaded = 1;

      -- Map existing is_downloaded (0) to Pending (0), which is already default

      PRAGMA user_version = 4;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v4 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v4.";
}

}  // namespace eillance::storage::migrations
