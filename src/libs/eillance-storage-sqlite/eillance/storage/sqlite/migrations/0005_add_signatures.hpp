#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>

#include "eillance/logging/logging.h"

namespace eillance::storage::migrations {

inline void migrate_to_v5(sqlite3* db) {
  LOGI << "Migrating to version 5...";
  const char* sql = R"SQL(
    BEGIN TRANSACTION;
    ALTER TABLE contacts ADD COLUMN signature_from_them TEXT;
    ALTER TABLE contacts ADD COLUMN my_signature_for_them TEXT;
    PRAGMA user_version = 5;
    COMMIT;
  )SQL";
  char* err_msg = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to run migration to v5: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LOGI << "Database migrated to version 5: Added signature columns to contacts table.";
}

}  // namespace eillance::storage::migrations
