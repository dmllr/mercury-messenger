#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0018(sqlite3* db) {
  LGV << "Migrating database schema to v18 (Ledger Signatures)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE ledger_entries ADD COLUMN signature BLOB;

      PRAGMA user_version = 18;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v18 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v18.";
}

}  // namespace eillance::storage::migrations
