#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <vector>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include <nlohmann/json.hpp>

namespace eillance::storage {

namespace {

constexpr size_t MaxInlineContentSize = 2048;
constexpr int CurrentDbVersion = 3;

class SQLiteStatement {
  sqlite3_stmt* stmt_ = nullptr;

public:
  explicit SQLiteStatement(sqlite3* db, const std::string& query) {
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
      LOGE << "SQLite prepare failed: " << sqlite3_errmsg(db);
      throw std::runtime_error("SQLite prepare failed");
    }
  }

  ~SQLiteStatement() {
    if (stmt_) {
      sqlite3_finalize(stmt_);
    }
  }

  SQLiteStatement(const SQLiteStatement&) = delete;

  auto operator=(const SQLiteStatement&) -> SQLiteStatement& = delete;

  SQLiteStatement(SQLiteStatement&& other) noexcept : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
  }

  auto operator=(SQLiteStatement&& other) noexcept -> SQLiteStatement& {
    if (this != &other) {
      if (stmt_) {
        sqlite3_finalize(stmt_);
      }
      stmt_ = other.stmt_;
      other.stmt_ = nullptr;
    }
    return *this;
  }

  auto get() const -> sqlite3_stmt* { return stmt_; }
};

void bind_text(sqlite3_stmt* stmt, const int idx, const std::string& val) {
  sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
}

void bind_optional_text(sqlite3_stmt* stmt, const int idx, const std::optional<std::string>& val) {
  if (val) {
    sqlite3_bind_text(stmt, idx, val->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, idx);
  }
}

auto column_string(sqlite3_stmt* stmt, const int idx) -> std::string {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
  return text ? std::string(text) : "";
}

auto column_optional_string(sqlite3_stmt* stmt, const int idx) -> std::optional<std::string> {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  return column_string(stmt, idx);
}

}  // namespace

class SQLiteStorage final : public PersistentStorageImpl {
  sqlite3* db_ = nullptr;
  std::recursive_mutex mutex_;

public:
  explicit SQLiteStorage(const std::filesystem::path& db_path) {
    try {
      if (sqlite3_open(db_path.string().c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to open database: ") + sqlite3_errmsg(db_));
      }
      sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
      sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
      RunMigrations();
    } catch (const std::exception& e) {
      LOGE << "Failed to initialize SQLite database at " << db_path.string() << ": " << e.what();
      if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
      }
      throw;
    }
  }

  ~SQLiteStorage() override {
    if (db_) {
      sqlite3_close_v2(db_);
    }
  }

  // --- Settings ---
  auto GetSetting(const std::string_view key) -> std::optional<std::string> override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
        SELECT
          value
        FROM
          settings
        WHERE
          key = ?;
    )SQL");
    bind_text(stmt.get(), 1, std::string(key));
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      return column_string(stmt.get(), 0);
    }
    return std::nullopt;
  }

  void SetSetting(const std::string_view key, const std::string_view value) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      REPLACE INTO settings
        (key, value)
      VALUES
        (?, ?);
    )SQL");
    bind_text(stmt.get(), 1, std::string(key));
    bind_text(stmt.get(), 2, std::string(value));
    sqlite3_step(stmt.get());
  }

  void DeleteSetting(const std::string_view key) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      DELETE FROM
        settings
      WHERE
        key = ?;
    )SQL");
    bind_text(stmt.get(), 1, std::string(key));
    sqlite3_step(stmt.get());
  }

  // --- Raw Key-Value for backward compatibility if needed ---
  auto GetRaw(const std::string& key) -> std::optional<std::vector<std::byte>> override {
    if (const auto val = GetSetting(key)) { return crypto::FromHex(*val); }
    return std::nullopt;
  }

  auto SetRaw(const std::string& key, const std::vector<std::byte>& value) -> bool override {
    SetSetting(key, crypto::ToHex(value));
    return true;
  }

  // --- Contacts ---
  auto GetContacts() -> std::vector<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    std::vector<identity::Contact> contacts;
    const SQLiteStatement stmt(db_, R"SQL(
      SELECT
        peer_id,
        status,
        display_name,
        display_name_override,
        bio,
        photo_base64
      FROM
        contacts;
    )SQL");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      contacts.push_back({.peer_id = column_string(stmt.get(), 0),
                          .status = static_cast<identity::ContactStatus>(sqlite3_column_int(
                              stmt.get(), 1)),
                          .display_name = column_optional_string(stmt.get(), 2),
                          .display_name_override = column_optional_string(stmt.get(), 3),
                          .bio = column_optional_string(stmt.get(), 4),
                          .photo_base64 = column_optional_string(stmt.get(), 5)});
    }
    return contacts;
  }

  auto GetContact(std::string_view peer_id) -> std::optional<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      SELECT
        peer_id,
        status,
        display_name,
        display_name_override,
        bio,
        photo_base64
      FROM
        contacts
      WHERE
        peer_id = ?;
    )SQL");
    bind_text(stmt.get(), 1, std::string(peer_id));
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      return identity::Contact{
          .peer_id = column_string(stmt.get(), 0),
          .status = static_cast<identity::ContactStatus>(sqlite3_column_int(stmt.get(), 1)),
          .display_name = column_optional_string(stmt.get(), 2),
          .display_name_override = column_optional_string(stmt.get(), 3),
          .bio = column_optional_string(stmt.get(), 4),
          .photo_base64 = column_optional_string(stmt.get(), 5)};
    }
    return std::nullopt;
  }

  void UpdateContact(const identity::Contact& contact) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      REPLACE INTO contacts
        (peer_id, status, display_name, display_name_override, bio, photo_base64)
      VALUES
        (?, ?, ?, ?, ?, ?);
    )SQL");
    bind_text(stmt.get(), 1, contact.peer_id);
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(contact.status));
    bind_optional_text(stmt.get(), 3, contact.display_name);
    bind_optional_text(stmt.get(), 4, contact.display_name_override);
    bind_optional_text(stmt.get(), 5, contact.bio);
    bind_optional_text(stmt.get(), 6, contact.photo_base64);
    sqlite3_step(stmt.get());
  }

  void RemoveContact(std::string_view peer_id) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      DELETE FROM
        contacts
      WHERE
        peer_id = ?;
    )SQL");
    bind_text(stmt.get(), 1, std::string(peer_id));
    sqlite3_step(stmt.get());
  }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      INSERT INTO history (
        peer_id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content,
        duration_seconds,
        call_direction,
        call_status
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    bind_text(stmt.get(), 1, entry.peer_id_for_db);
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(entry.kind));
    sqlite3_bind_int64(stmt.get(), 3, entry.timestamp);
    sqlite3_bind_int(stmt.get(), 4, static_cast<int>(entry.direction));
    sqlite3_bind_int(stmt.get(), 5, static_cast<int>(entry.status));

    std::optional<std::string> full_content_for_storage;

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      if (msg_entry->content.length() > MaxInlineContentSize) {
        entry.is_truncated = true;
        full_content_for_storage = msg_entry->content;
        msg_entry->content = full_content_for_storage->substr(0, MaxInlineContentSize);
      } else {
        entry.is_truncated = false;
      }
      sqlite3_bind_int(stmt.get(), 6, entry.is_truncated);
      bind_text(stmt.get(), 7, msg_entry->content);
      sqlite3_bind_int(stmt.get(), 8, 0);
      sqlite3_bind_int(stmt.get(), 9, 0);
      sqlite3_bind_int(stmt.get(), 10, 0);
    } else if (const auto* call_entry = std::get_if<messaging::CallEntry>(&entry.data)) {
      entry.is_truncated = false;
      sqlite3_bind_int(stmt.get(), 6, 0);  // is_truncated = false
      sqlite3_bind_null(stmt.get(), 7);
      sqlite3_bind_int(stmt.get(), 8, call_entry->duration_seconds);
      sqlite3_bind_int(stmt.get(), 9, static_cast<int>(call_entry->call_direction));
      sqlite3_bind_int(stmt.get(), 10, static_cast<int>(call_entry->call_status));
    }

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
      LOGE << "Failed to insert history entry: " << sqlite3_errmsg(db_);
      return {};  // Return empty record on failure
    }

    entry.id = sqlite3_last_insert_rowid(db_);

    if (full_content_for_storage) {
      const SQLiteStatement large_msg_stmt(db_, R"SQL(
        INSERT INTO large_messages
          (history_id, full_content)
        VALUES
          (?, ?);
        )SQL");
      sqlite3_bind_int64(large_msg_stmt.get(), 1, entry.id);
      bind_text(large_msg_stmt.get(), 2, *full_content_for_storage);
      if (sqlite3_step(large_msg_stmt.get()) != SQLITE_DONE) {
        LOGE << "Failed to insert large message content: " << sqlite3_errmsg(db_);
      }
    }

    return entry;
  }

  void UpdateHistoryEntry(const messaging::HistoryEntryRecord& entry) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      UPDATE
        history
      SET
        kind = ?,
        timestamp = ?,
        direction = ?,
        status = ?,
        content = ?,
        duration_seconds = ?,
        call_direction = ?,
        call_status = ?
      WHERE
        id = ?;
    )SQL");

    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(entry.kind));
    sqlite3_bind_int64(stmt.get(), 2, entry.timestamp);
    sqlite3_bind_int(stmt.get(), 3, static_cast<int>(entry.direction));
    sqlite3_bind_int(stmt.get(), 4, static_cast<int>(entry.status));

    std::visit([&]<typename T0>(T0&& arg) {
      using T = std::decay_t<T0>;
      if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
        bind_text(stmt.get(), 5, arg.content);
        sqlite3_bind_int(stmt.get(), 6, 0);
        sqlite3_bind_int(stmt.get(), 7, 0);
        sqlite3_bind_int(stmt.get(), 8, 0);
      } else if constexpr (std::is_same_v<T, messaging::CallEntry>) {
        sqlite3_bind_null(stmt.get(), 5);
        sqlite3_bind_int(stmt.get(), 6, arg.duration_seconds);
        sqlite3_bind_int(stmt.get(), 7, static_cast<int>(arg.call_direction));
        sqlite3_bind_int(stmt.get(), 8, static_cast<int>(arg.call_status));
      }
    },
               entry.data);

    sqlite3_bind_int64(stmt.get(), 9, entry.id);
    sqlite3_step(stmt.get());
  }

  void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id,
                           messaging::MessageStatus new_status) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      UPDATE
        history
      SET
        status = ?
      WHERE
        id = ? AND peer_id = ?;
    )SQL");
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(new_status));
    sqlite3_bind_int64(stmt.get(), 2, message_id);
    bind_text(stmt.get(), 3, peer_id);
    sqlite3_step(stmt.get());
  }

  void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids,
                             messaging::MessageStatus new_status) override {
    if (message_ids.empty()) {
      return;
    }
    const std::lock_guard lock(mutex_);
    nlohmann::json ids_json = message_ids;
    const std::string query = R"SQL(
      UPDATE
        history
      SET
        status = ?
      WHERE
        peer_id = ? AND id IN (SELECT value FROM json_each(?));
    )SQL";

    const SQLiteStatement stmt(db_, query);
    sqlite3_bind_int(stmt.get(), 1, static_cast<int>(new_status));
    bind_text(stmt.get(), 2, peer_id);
    bind_text(stmt.get(), 3, ids_json.dump());
    sqlite3_step(stmt.get());
  }

  auto GetHistoryPage(const std::string& peer_id, size_t page_index,
                      size_t page_size) -> messaging::HistoryPage override {
    const std::lock_guard lock(mutex_);
    messaging::HistoryPage page;

    {
      const SQLiteStatement count_stmt(db_, R"SQL(
        SELECT
          count(*)
        FROM
          history
        WHERE
          peer_id = ?;
      )SQL");
      bind_text(count_stmt.get(), 1, peer_id);
      if (sqlite3_step(count_stmt.get()) == SQLITE_ROW) {
        page.total_messages = sqlite3_column_int64(count_stmt.get(), 0);
      }
    }

    const SQLiteStatement stmt(db_, R"SQL(
      SELECT
        id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content,
        duration_seconds,
        call_direction,
        call_status
      FROM
        history
      WHERE
        peer_id = ?
      ORDER BY
        timestamp DESC
      LIMIT ? OFFSET ?;
    )SQL");
    bind_text(stmt.get(), 1, peer_id);
    sqlite3_bind_int64(stmt.get(), 2, page_size);
    sqlite3_bind_int64(stmt.get(), 3, page_index * page_size);

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      page.messages.push_back(ParseHistoryEntry(stmt.get()));
    }
    std::ranges::reverse(page.messages);  // Reverse to get ascending time order for display
    return page;
  }

  auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      SELECT
        full_content
      FROM
        large_messages
      WHERE
        history_id = ?;
    )SQL");
    sqlite3_bind_int64(stmt.get(), 1, message_id);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      return column_string(stmt.get(), 0);
    }
    return std::nullopt;
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<messaging::ChatSummary> override {
    if (peer_ids.empty()) {
      return {};
    }
    const std::lock_guard lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (const auto& peer_id: peer_ids) {
      messaging::ChatSummary summary;
      summary.peer_id = peer_id;
      summary.unread_count = 0;

      // Query 1: Get unread count
      {
        const SQLiteStatement unread_stmt(db_, R"SQL(
          SELECT
            COUNT(*)
          FROM
            history
          WHERE
            peer_id = ?
            AND direction = ?
            AND status != ?
            AND kind = ?;
        )SQL");

        int bind_idx = 1;
        bind_text(unread_stmt.get(), bind_idx++, peer_id);
        sqlite3_bind_int(unread_stmt.get(), bind_idx++, static_cast<int>(messaging::MessageDirection::received));
        sqlite3_bind_int(unread_stmt.get(), bind_idx++, static_cast<int>(messaging::MessageStatus::read));
        sqlite3_bind_int(unread_stmt.get(), bind_idx++, static_cast<int>(messaging::HistoryEntryKind::textMessage));

        if (sqlite3_step(unread_stmt.get()) == SQLITE_ROW) {
          summary.unread_count = sqlite3_column_int(unread_stmt.get(), 0);
        }
      }

      // Query 2: Get last message
      {
        const SQLiteStatement last_msg_stmt(db_, R"SQL(
          SELECT
            id,
            kind,
            timestamp,
            direction,
            status,
            is_truncated,
            content,
            duration_seconds,
            call_direction,
            call_status
          FROM
            history
          WHERE
            peer_id = ?
          ORDER BY
            timestamp DESC
          LIMIT 1;
        )SQL");
        bind_text(last_msg_stmt.get(), 1, peer_id);

        if (sqlite3_step(last_msg_stmt.get()) == SQLITE_ROW) {
          summary.last_message = ParseHistoryEntry(last_msg_stmt.get());
          summaries.push_back(summary);
        }
      }
    }

    std::ranges::sort(summaries, [](const auto& a, const auto& b) {
      return a.last_message.timestamp > b.last_message.timestamp;
    });
    return summaries;
  }

  auto GetMessagesWithStatus(const std::string& peer_id,
                             messaging::MessageStatus status) -> std::vector<messaging::HistoryEntryRecord> override {
    const std::lock_guard lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> records;
    const SQLiteStatement stmt(db_, R"SQL(
      SELECT
        id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content,
        duration_seconds,
        call_direction,
        call_status
      FROM
        history
      WHERE
        peer_id = ? AND status = ?;
    )SQL");
    bind_text(stmt.get(), 1, peer_id);
    sqlite3_bind_int(stmt.get(), 2, static_cast<int>(status));

    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      records.push_back(ParseHistoryEntry(stmt.get()));
    }
    return records;
  }

private:
  void RunMigrations() {
    int user_version = 0;
    const SQLiteStatement get_version_stmt(db_, "PRAGMA user_version;");
    if (sqlite3_step(get_version_stmt.get()) == SQLITE_ROW) {
      user_version = sqlite3_column_int(get_version_stmt.get(), 0);
    }

    if (user_version < 1) {
      MigrateToV1();
    }
    if (user_version < 2) {
      MigrateToV2();
    }
    if (user_version < 3) {
      MigrateToV3();
    }

    if (user_version != CurrentDbVersion) {
      std::string set_version_sql =
          "PRAGMA user_version = " + std::to_string(CurrentDbVersion) + ";";
      char* err_msg = nullptr;
      if (sqlite3_exec(db_, set_version_sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string error = "Failed to set DB version: ";
        error += err_msg;
        sqlite3_free(err_msg);
        throw std::runtime_error(error);
      }
    }
  }

  void MigrateToV1() {
    LOGI << "Database version is 0, migrating to version 1...";
    const char* schema = R"SQL(
      BEGIN TRANSACTION;
      CREATE TABLE IF NOT EXISTS settings (
        key   TEXT PRIMARY KEY NOT NULL,
        value TEXT
      );
      CREATE TABLE IF NOT EXISTS contacts (
        peer_id               TEXT PRIMARY KEY NOT NULL,
        status                INTEGER NOT NULL,
        display_name          TEXT,
        display_name_override TEXT,
        bio                   TEXT,
        photo_base64          TEXT
      );
      CREATE TABLE IF NOT EXISTS history (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        peer_id          TEXT NOT NULL,
        kind             INTEGER NOT NULL,
        timestamp        INTEGER NOT NULL,
        direction        INTEGER NOT NULL,
        status           INTEGER NOT NULL,
        content          TEXT,
        duration_seconds INTEGER,
        call_direction   INTEGER,
        call_status      INTEGER
      );
      CREATE INDEX IF NOT EXISTS idx_history_peer_timestamp ON history (peer_id, timestamp);
      COMMIT;
    )SQL";

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      std::string error = "Failed to execute migration to V1: ";
      error += err_msg;
      sqlite3_free(err_msg);
      throw std::runtime_error(error);
    }
    LOGI << "Migration to V1 successful.";
  }

  void MigrateToV2() {
    LOGI << "Database version is 1, migrating to version 2...";
    const char* schema = R"SQL(
      BEGIN TRANSACTION;
      ALTER TABLE history ADD COLUMN is_truncated INTEGER NOT NULL DEFAULT 0;
      CREATE TABLE large_messages (
        history_id   INTEGER PRIMARY KEY NOT NULL,
        full_content TEXT,
        FOREIGN KEY(history_id) REFERENCES history(id) ON DELETE CASCADE
      );
      COMMIT;
    )SQL";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      std::string error = "Failed to execute migration to V2: ";
      error += err_msg;
      sqlite3_free(err_msg);
      throw std::runtime_error(error);
    }
    LOGI << "Migration to V2 successful.";
  }

  void MigrateToV3() {
    LOGI << "Database version is 2, migrating to version 3...";
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
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
      SQLiteStatement select_stmt(db_, select_sql);
      sqlite3_bind_int(select_stmt.get(), 1, static_cast<int>(messaging::HistoryEntryKind::textMessage));
      sqlite3_bind_int(select_stmt.get(), 2, static_cast<int>(messaging::HistoryEntryKind::systemMessage));
      sqlite3_bind_int(select_stmt.get(), 3, MaxInlineContentSize);

      SQLiteStatement insert_stmt(db_, R"SQL(
        INSERT INTO large_messages
          (history_id, full_content)
        VALUES
          (?, ?);
      )SQL");
      SQLiteStatement update_stmt(db_, R"SQL(
        UPDATE
          history
        SET
          content = ?,
          is_truncated = 1
        WHERE
          id = ?;
      )SQL");

      while (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
        const int64_t id = sqlite3_column_int64(select_stmt.get(), 0);
        const std::string full_content = column_string(select_stmt.get(), 1);
        const std::string preview = full_content.substr(0, MaxInlineContentSize);

        // Insert full content into large_messages
        sqlite3_reset(insert_stmt.get());
        sqlite3_bind_int64(insert_stmt.get(), 1, id);
        bind_text(insert_stmt.get(), 2, full_content);
        if (sqlite3_step(insert_stmt.get()) != SQLITE_DONE) {
          throw std::runtime_error("Failed to insert into large_messages during V3 migration.");
        }

        // Update history with truncated content
        sqlite3_reset(update_stmt.get());
        bind_text(update_stmt.get(), 1, preview);
        sqlite3_bind_int64(update_stmt.get(), 2, id);
        if (sqlite3_step(update_stmt.get()) != SQLITE_DONE) {
          throw std::runtime_error("Failed to update history during V3 migration.");
        }
      }
    } catch (const std::exception& e) {
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      LOGE << "Migration to V3 failed: " << e.what();
      throw;
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to commit V3 migration transaction.");
    }
    LOGI << "Migration to V3 successful.";
  }

  static auto ParseHistoryEntry(sqlite3_stmt* stmt) -> messaging::HistoryEntryRecord {
    messaging::HistoryEntryRecord record{
        .id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
        .kind = static_cast<messaging::HistoryEntryKind>(sqlite3_column_int(stmt, 1)),
        .timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2)),
        .direction = static_cast<messaging::MessageDirection>(sqlite3_column_int(stmt, 3)),
        .status = static_cast<messaging::MessageStatus>(sqlite3_column_int(stmt, 4)),
        .is_truncated = (sqlite3_column_int(stmt, 5) == 1),
    };

    switch (record.kind) {
      case messaging::HistoryEntryKind::textMessage:
      case messaging::HistoryEntryKind::systemMessage:
        record.data = messaging::MessageEntry{
            .content = column_string(stmt, 6)};
        break;
      case messaging::HistoryEntryKind::voiceCall:
      case messaging::HistoryEntryKind::videoCall:
        record.data = messaging::CallEntry{
            .duration_seconds = static_cast<uint32_t>(sqlite3_column_int(stmt, 7)),
            .call_direction = static_cast<messaging::CallDirection>(sqlite3_column_int(stmt, 8)),
            .call_status = static_cast<messaging::CallStatus>(sqlite3_column_int(stmt, 9))};
        break;
    }
    return record;
  }
};

}  // namespace eillance::storage
