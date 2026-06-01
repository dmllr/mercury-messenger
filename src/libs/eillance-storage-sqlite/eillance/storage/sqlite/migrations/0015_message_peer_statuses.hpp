#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0015(sqlite3* db) {
  LGV << "Migrating database schema to v15 (Message Peer Statuses)...";
  auto const* const schema = R"SQL(
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS message_peer_statuses (
        topic_id TEXT NOT NULL,
        message_id INTEGER NOT NULL,
        peer_id TEXT NOT NULL,
        status INTEGER NOT NULL,
        PRIMARY KEY (topic_id, message_id, peer_id),
        FOREIGN KEY(topic_id, message_id) REFERENCES messages(peer_id, id) ON DELETE CASCADE
      );

      PRAGMA user_version = 15;
      COMMIT;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v15 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v15.";
}

}  // namespace eillance::storage::migrations
