#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0013(sqlite3* db) {
  LGV << "Migrating database schema to v13 (Group Chats)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS groups (
        id TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL,
        owner_id TEXT NOT NULL
      );

      CREATE TABLE IF NOT EXISTS group_members (
        group_id TEXT NOT NULL,
        contact_id TEXT NOT NULL,
        role INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (group_id, contact_id),
        FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE
      );

      PRAGMA user_version = 13;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v13 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v13.";
}

}  // namespace eillance::storage::migrations
