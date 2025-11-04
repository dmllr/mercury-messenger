#pragma once

#include <sqlite3.h>
#include <stdexcept>
#include <string>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/sqlite_helpers.hpp"

namespace eillance::storage::migrations {

inline void migrate_to_v3(sqlite3* db, const size_t max_inline_content_size) {
  LOGI << "Database version is 2, migrating to version 3...";
  if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to begin V3 migration transaction.");
  }

  try {
    const std::string select_sql = R"SQL(
        SELECT
          id,
          content
        FROM
          history
        WHERE
          kind IN (?, ?)
          AND LENGTH(content) > ?
          AND is_truncated = 0;
      )SQL";
    const SQLiteStatement select_stmt(db, select_sql);
    sqlite3_bind_int(select_stmt.Get(), 1, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int(select_stmt.Get(), 2,
                     static_cast<int>(messaging::HistoryEntryKind::systemMessage));
    sqlite3_bind_int(select_stmt.Get(), 3, static_cast<int>(max_inline_content_size));

    const SQLiteStatement insert_stmt(db, R"SQL(
        INSERT INTO large_messages
          (history_id, full_content)
        VALUES
          (?, ?);
      )SQL");
    const SQLiteStatement update_stmt(db, R"SQL(
        UPDATE
          history
        SET
          content = ?,
          is_truncated = 1
        WHERE
          id = ?;
      )SQL");

    while (sqlite3_step(select_stmt.Get()) == SQLITE_ROW) {
      const int64_t id = sqlite3_column_int64(select_stmt.Get(), 0);
      const std::string full_content = ColumnString(select_stmt.Get(), 1);
      const std::string preview = full_content.substr(0, max_inline_content_size);

      // Insert full content into large_messages
      sqlite3_reset(insert_stmt.Get());
      sqlite3_bind_int64(insert_stmt.Get(), 1, id);
      BindText(insert_stmt.Get(), 2, full_content);
      if (sqlite3_step(insert_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error("Failed to insert into large_messages during V3 migration.");
      }

      // Update history with truncated content
      sqlite3_reset(update_stmt.Get());
      BindText(update_stmt.Get(), 1, preview);
      sqlite3_bind_int64(update_stmt.Get(), 2, id);
      if (sqlite3_step(update_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error("Failed to update history during V3 migration.");
      }
    }
  } catch (const std::exception& e) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    LOGE << "Migration to V3 failed: " << e.what();
    throw;
  }

  if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to commit V3 migration transaction.");
  }
  LOGI << "Migration to V3 successful.";
}

}  // namespace eillance::storage::migrations
