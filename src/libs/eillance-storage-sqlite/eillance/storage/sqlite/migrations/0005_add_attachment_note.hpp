#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0005(sqlite3* db) {
  LGV << "Migrating database schema to v5 (Attachment Notes)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE message_attachments ADD COLUMN note TEXT;

      PRAGMA user_version = 5;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v5 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v5.";
}

}  // namespace eillance::storage::migrations
