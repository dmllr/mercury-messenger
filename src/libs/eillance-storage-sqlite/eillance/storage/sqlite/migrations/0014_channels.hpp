#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0014(sqlite3* db) {
  LGV << "Migrating database schema to v14 (Public Channels)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS channels (
        id TEXT PRIMARY KEY NOT NULL,
        name TEXT NOT NULL,
        description TEXT,
        owner_id TEXT NOT NULL,
        is_public INTEGER NOT NULL DEFAULT 1
      );
      CREATE TABLE IF NOT EXISTS channel_members (
        channel_id TEXT NOT NULL,
        contact_id TEXT NOT NULL,
        role INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (channel_id, contact_id),
        FOREIGN KEY(channel_id) REFERENCES channels(id) ON DELETE CASCADE
      );

      PRAGMA user_version = 14;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v14 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v14.";
}

}  // namespace eillance::storage::migrations
