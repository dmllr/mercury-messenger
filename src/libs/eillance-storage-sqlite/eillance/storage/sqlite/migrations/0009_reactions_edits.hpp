#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0009(sqlite3* db) {
  LGV << "Migrating database schema to v9 (Reactions, Edits & Indexes)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      ALTER TABLE messages ADD COLUMN is_edited INTEGER DEFAULT 0;
      ALTER TABLE messages ADD COLUMN is_deleted INTEGER DEFAULT 0;

      CREATE TABLE IF NOT EXISTS reactions (
        peer_id    TEXT NOT NULL,
        message_id INTEGER NOT NULL,
        author_id  TEXT NOT NULL,
        reaction   INTEGER NOT NULL,
        PRIMARY KEY (peer_id, message_id, author_id, reaction),
        FOREIGN KEY(peer_id, message_id) REFERENCES messages(peer_id, id) ON DELETE CASCADE
      );

      -- Index for efficient lookup of messages by remote_id (needed for incoming edit/delete/reaction actions)
      CREATE INDEX IF NOT EXISTS idx_messages_remote_id ON messages (peer_id, remote_id);

      PRAGMA user_version = 9;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v9 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v9.";
}

}  // namespace eillance::storage::migrations
