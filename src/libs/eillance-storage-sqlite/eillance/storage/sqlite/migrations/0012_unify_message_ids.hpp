#pragma once

#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage::migrations {

inline void MigrateToV0012(sqlite3* db) {
  LGV << "Migrating database schema to v12 (Unify Message IDs)...";
  auto const* const schema = R"SQL(
      PRAGMA foreign_keys=OFF;
      BEGIN TRANSACTION;

      CREATE TEMP TABLE id_map (
          peer_id TEXT,
          old_id INTEGER,
          new_id INTEGER,
          PRIMARY KEY(peer_id, old_id)
      );

      -- Deterministically align old identities using (timestamp_ms << 19) | COALESCE(remote_id, local_id) % 524288
      -- with a fallback correlated subquery to strictly ensure uniqueness locally and avoid constraint failures.
      INSERT INTO id_map (peer_id, old_id, new_id)
      SELECT m1.peer_id, m1.id, 
             (((m1.timestamp / 1000) << 19) | (COALESCE(m1.remote_id, m1.id) % 524288)) +
             (SELECT COUNT(*) FROM messages m2 
              WHERE m2.peer_id = m1.peer_id 
                AND (((m2.timestamp / 1000) << 19) | (COALESCE(m2.remote_id, m2.id) % 524288)) = 
                    (((m1.timestamp / 1000) << 19) | (COALESCE(m1.remote_id, m1.id) % 524288))
                AND m2.id < m1.id)
      FROM messages m1;

      UPDATE large_messages
      SET message_id = (SELECT new_id FROM id_map WHERE id_map.peer_id = large_messages.peer_id AND id_map.old_id = large_messages.message_id)
      WHERE EXISTS (SELECT 1 FROM id_map WHERE id_map.peer_id = large_messages.peer_id AND id_map.old_id = large_messages.message_id);

      UPDATE message_attachments
      SET message_id = (SELECT new_id FROM id_map WHERE id_map.peer_id = message_attachments.peer_id AND id_map.old_id = message_attachments.message_id)
      WHERE EXISTS (SELECT 1 FROM id_map WHERE id_map.peer_id = message_attachments.peer_id AND id_map.old_id = message_attachments.message_id);

      UPDATE reactions
      SET message_id = (SELECT new_id FROM id_map WHERE id_map.peer_id = reactions.peer_id AND id_map.old_id = reactions.message_id)
      WHERE EXISTS (SELECT 1 FROM id_map WHERE id_map.peer_id = reactions.peer_id AND id_map.old_id = reactions.message_id);

      UPDATE messages
      SET reply_to_id = (SELECT new_id FROM id_map WHERE id_map.peer_id = messages.peer_id AND id_map.old_id = messages.reply_to_id)
      WHERE reply_to_id != 0 AND EXISTS (SELECT 1 FROM id_map WHERE id_map.peer_id = messages.peer_id AND id_map.old_id = messages.reply_to_id);

      CREATE TABLE messages_new (
          peer_id          TEXT NOT NULL,
          id               INTEGER NOT NULL,
          kind             INTEGER NOT NULL,
          timestamp        INTEGER NOT NULL,
          direction        INTEGER NOT NULL,
          status           INTEGER NOT NULL,
          content          TEXT,
          is_truncated     INTEGER NOT NULL DEFAULT 0,
          is_edited        INTEGER DEFAULT 0,
          is_deleted       INTEGER DEFAULT 0,
          reply_to_id      INTEGER DEFAULT 0,
          PRIMARY KEY (peer_id, id)
      );

      INSERT INTO messages_new (peer_id, id, kind, timestamp, direction, status, content, is_truncated, is_edited, is_deleted, reply_to_id)
      SELECT m.peer_id, i.new_id, m.kind, m.timestamp, m.direction, m.status, m.content, m.is_truncated, m.is_edited, m.is_deleted, m.reply_to_id
      FROM messages m
      JOIN id_map i ON m.peer_id = i.peer_id AND m.id = i.old_id;

      DROP TABLE messages;
      ALTER TABLE messages_new RENAME TO messages;
      CREATE INDEX idx_messages_peer_timestamp ON messages (peer_id, timestamp);

      DROP TABLE id_map;

      PRAGMA user_version = 12;
      COMMIT;
      PRAGMA foreign_keys=ON;
    )SQL";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to execute v12 migration: ";
    error += err_msg;
    sqlite3_free(err_msg);
    throw std::runtime_error(error);
  }
  LGD << "Database schema updated to v12.";
}

}  // namespace eillance::storage::migrations
