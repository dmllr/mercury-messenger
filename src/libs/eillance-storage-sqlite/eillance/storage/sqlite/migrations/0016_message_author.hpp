#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0016(sqlite3* db) {
  LGV << "Migrating database schema to v16 (Message Author ID)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;
      ALTER TABLE messages ADD COLUMN author_id TEXT DEFAULT '';
      UPDATE messages SET author_id = peer_id WHERE direction = 1 AND author_id = '';
      PRAGMA user_version = 16;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v16 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v16.";
}

}  // namespace eillance::storage::migrations
