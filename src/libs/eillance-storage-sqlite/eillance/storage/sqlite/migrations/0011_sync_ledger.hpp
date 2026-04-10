#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0011(sqlite3* db) {
  LGV << "Migrating database schema to v11 (Sync Ledger)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS ledger_entries (
        seq_id INTEGER PRIMARY KEY AUTOINCREMENT,
        topic_id TEXT NOT NULL,
        author_id TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        type INTEGER NOT NULL,
        payload TEXT NOT NULL,
        prev_hash TEXT NOT NULL,
        hash TEXT NOT NULL UNIQUE
      );
      CREATE INDEX IF NOT EXISTS idx_ledger_topic ON ledger_entries (topic_id, seq_id);

      CREATE TABLE IF NOT EXISTS sync_requests (
        topic_id TEXT NOT NULL,
        target_peer_id TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        last_attempt INTEGER NOT NULL,
        PRIMARY KEY (topic_id, target_peer_id)
      );

      DROP TABLE IF EXISTS pending_actions;

      PRAGMA user_version = 11;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v11 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v11.";
}

}  // namespace eillance::storage::migrations
