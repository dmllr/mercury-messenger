#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0006(sqlite3* db) {
  LGV << "Migrating database schema to v6 (Attachment Filenames)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE message_attachments ADD COLUMN file_name TEXT;

      PRAGMA user_version = 6;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v6 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v6.";
}

}  // namespace eillance::storage::migrations
