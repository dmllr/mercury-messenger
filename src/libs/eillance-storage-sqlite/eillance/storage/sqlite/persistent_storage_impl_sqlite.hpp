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
#include "eillance/storage/sqlite/migrations.hpp"
#include "eillance/storage/sqlite/sqlite_helpers.hpp"

#include <nlohmann/json.hpp>

namespace eillance::storage {

namespace {

constexpr size_t MaxInlineContentSize = 2048;
constexpr int CurrentDbVersion = 5;

}  // namespace

class SQLiteStorage final : public PersistentStorageImpl {
  sqlite3* db_ = nullptr;
  std::recursive_mutex mutex_;
  std::filesystem::path db_path_;

public:
  explicit SQLiteStorage(const std::filesystem::path& db_path) : db_path_(db_path) {
    try {
      std::filesystem::create_directories(db_path_.parent_path());
      if (sqlite3_open(db_path_.string().c_str(), &db_) != SQLITE_OK) {
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

  [[nodiscard]] auto GetPath() const -> std::string override {
    return db_path_.string();
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
    BindText(stmt.Get(), 1, std::string(key));
    if (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      return ColumnString(stmt.Get(), 0);
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
    BindText(stmt.Get(), 1, std::string(key));
    BindText(stmt.Get(), 2, std::string(value));
    sqlite3_step(stmt.Get());
  }

  void DeleteSetting(const std::string_view key) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      DELETE FROM
        settings
      WHERE
        key = ?;
    )SQL");
    BindText(stmt.Get(), 1, std::string(key));
    sqlite3_step(stmt.Get());
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
        photo_base64,
        signature_from_them,
        my_signature_for_them
      FROM
        contacts;
    )SQL");
    while (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      contacts.push_back({.peer_id = ColumnString(stmt.Get(), 0),
                          .status = static_cast<identity::ContactStatus>(sqlite3_column_int(stmt.Get(), 1)),
                          .display_name = ColumnOptionalString(stmt.Get(), 2),
                          .display_name_override = ColumnOptionalString(stmt.Get(), 3),
                          .bio = ColumnOptionalString(stmt.Get(), 4),
                          .photo_base64 = ColumnOptionalString(stmt.Get(), 5),
                          .signature_from_them = ColumnOptionalString(stmt.Get(), 6),
                          .my_signature_for_them = ColumnOptionalString(stmt.Get(), 7)});
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
        photo_base64,
        signature_from_them,
        my_signature_for_them
      FROM
        contacts
      WHERE
        peer_id = ?;
    )SQL");
    BindText(stmt.Get(), 1, std::string(peer_id));
    if (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      return identity::Contact{.peer_id = ColumnString(stmt.Get(), 0),
                               .status = static_cast<identity::ContactStatus>(sqlite3_column_int(stmt.Get(), 1)),
                               .display_name = ColumnOptionalString(stmt.Get(), 2),
                               .display_name_override = ColumnOptionalString(stmt.Get(), 3),
                               .bio = ColumnOptionalString(stmt.Get(), 4),
                               .photo_base64 = ColumnOptionalString(stmt.Get(), 5),
                               .signature_from_them = ColumnOptionalString(stmt.Get(), 6),
                               .my_signature_for_them = ColumnOptionalString(stmt.Get(), 7)};
    }
    return std::nullopt;
  }

  void UpdateContact(const identity::Contact& contact) override {
    const std::lock_guard lock(mutex_);
    const SQLiteStatement stmt(db_, R"SQL(
      REPLACE INTO contacts
        (peer_id, status, display_name, display_name_override, bio, photo_base64, signature_from_them, my_signature_for_them)
      VALUES
        (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");
    BindText(stmt.Get(), 1, contact.peer_id);
    sqlite3_bind_int(stmt.Get(), 2, static_cast<int>(contact.status));
    BindOptionalText(stmt.Get(), 3, contact.display_name);
    BindOptionalText(stmt.Get(), 4, contact.display_name_override);
    BindOptionalText(stmt.Get(), 5, contact.bio);
    BindOptionalText(stmt.Get(), 6, contact.photo_base64);
    BindOptionalText(stmt.Get(), 7, contact.signature_from_them);
    BindOptionalText(stmt.Get(), 8, contact.my_signature_for_them);
    sqlite3_step(stmt.Get());
  }

  void RemoveContact(std::string_view peer_id) override {
    const std::lock_guard lock(mutex_);
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LOGE << "Failed to begin transaction for contact removal: " << (err_msg ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      return;
    }

    try {
      const SQLiteStatement history_stmt(db_, "DELETE FROM history WHERE peer_id = ?;");
      BindText(history_stmt.Get(), 1, std::string(peer_id));
      if (sqlite3_step(history_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }

      const SQLiteStatement contact_stmt(db_, "DELETE FROM contacts WHERE peer_id = ?;");
      BindText(contact_stmt.Get(), 1, std::string(peer_id));
      if (sqlite3_step(contact_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }
    } catch (const std::runtime_error& e) {
      LOGE << "Error during contact removal: " << e.what();
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      return;
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LOGE << "Failed to commit transaction for contact removal: " << (err_msg ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry)
      -> messaging::HistoryEntryRecord override {
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

    BindText(stmt.Get(), 1, entry.peer_id_for_db);
    sqlite3_bind_int(stmt.Get(), 2, static_cast<int>(entry.kind));
    sqlite3_bind_int64(stmt.Get(), 3, entry.timestamp);
    sqlite3_bind_int(stmt.Get(), 4, static_cast<int>(entry.direction));
    sqlite3_bind_int(stmt.Get(), 5, static_cast<int>(entry.status));

    std::optional<std::string> full_content_for_storage;

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      if (msg_entry->content.length() > MaxInlineContentSize) {
        entry.is_truncated = true;
        full_content_for_storage = msg_entry->content;
        msg_entry->content = full_content_for_storage->substr(0, MaxInlineContentSize);
      } else {
        entry.is_truncated = false;
      }
      sqlite3_bind_int(stmt.Get(), 6, entry.is_truncated);
      BindText(stmt.Get(), 7, msg_entry->content);
      sqlite3_bind_int(stmt.Get(), 8, 0);
      sqlite3_bind_int(stmt.Get(), 9, 0);
      sqlite3_bind_int(stmt.Get(), 10, 0);
    } else if (const auto* call_entry = std::get_if<messaging::CallEntry>(&entry.data)) {
      entry.is_truncated = false;
      sqlite3_bind_int(stmt.Get(), 6, 0);  // is_truncated = false
      sqlite3_bind_null(stmt.Get(), 7);
      sqlite3_bind_int(stmt.Get(), 8, call_entry->duration_seconds);
      sqlite3_bind_int(stmt.Get(), 9, static_cast<int>(call_entry->call_direction));
      sqlite3_bind_int(stmt.Get(), 10, static_cast<int>(call_entry->call_status));
    }

    if (sqlite3_step(stmt.Get()) != SQLITE_DONE) {
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
      sqlite3_bind_int64(large_msg_stmt.Get(), 1, entry.id);
      BindText(large_msg_stmt.Get(), 2, *full_content_for_storage);
      if (sqlite3_step(large_msg_stmt.Get()) != SQLITE_DONE) {
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

    sqlite3_bind_int(stmt.Get(), 1, static_cast<int>(entry.kind));
    sqlite3_bind_int64(stmt.Get(), 2, entry.timestamp);
    sqlite3_bind_int(stmt.Get(), 3, static_cast<int>(entry.direction));
    sqlite3_bind_int(stmt.Get(), 4, static_cast<int>(entry.status));

    std::visit([&]<typename T0>(T0&& arg) {
      using T = std::decay_t<T0>;
      if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
        BindText(stmt.Get(), 5, arg.content);
        sqlite3_bind_int(stmt.Get(), 6, 0);
        sqlite3_bind_int(stmt.Get(), 7, 0);
        sqlite3_bind_int(stmt.Get(), 8, 0);
      } else if constexpr (std::is_same_v<T, messaging::CallEntry>) {
        sqlite3_bind_null(stmt.Get(), 5);
        sqlite3_bind_int(stmt.Get(), 6, arg.duration_seconds);
        sqlite3_bind_int(stmt.Get(), 7, static_cast<int>(arg.call_direction));
        sqlite3_bind_int(stmt.Get(), 8, static_cast<int>(arg.call_status));
      }
    },
               entry.data);

    sqlite3_bind_int64(stmt.Get(), 9, entry.id);
    sqlite3_step(stmt.Get());
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
    sqlite3_bind_int(stmt.Get(), 1, static_cast<int>(new_status));
    sqlite3_bind_int64(stmt.Get(), 2, message_id);
    BindText(stmt.Get(), 3, peer_id);
    sqlite3_step(stmt.Get());
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
    sqlite3_bind_int(stmt.Get(), 1, static_cast<int>(new_status));
    BindText(stmt.Get(), 2, peer_id);
    BindText(stmt.Get(), 3, ids_json.dump());
    sqlite3_step(stmt.Get());
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
      BindText(count_stmt.Get(), 1, peer_id);
      if (sqlite3_step(count_stmt.Get()) == SQLITE_ROW) {
        page.total_messages = sqlite3_column_int64(count_stmt.Get(), 0);
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
    BindText(stmt.Get(), 1, peer_id);
    sqlite3_bind_int64(stmt.Get(), 2, page_size);
    sqlite3_bind_int64(stmt.Get(), 3, page_index * page_size);

    while (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      page.messages.push_back(ParseHistoryEntry(stmt.Get()));
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
    sqlite3_bind_int64(stmt.Get(), 1, message_id);
    if (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      return ColumnString(stmt.Get(), 0);
    }
    return std::nullopt;
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids)
      -> std::vector<messaging::ChatSummary> override {
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
        BindText(unread_stmt.Get(), bind_idx++, peer_id);
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++,
                         static_cast<int>(messaging::MessageDirection::received));
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++,
                         static_cast<int>(messaging::MessageStatus::read));
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++,
                         static_cast<int>(messaging::HistoryEntryKind::textMessage));

        if (sqlite3_step(unread_stmt.Get()) == SQLITE_ROW) {
          summary.unread_count = sqlite3_column_int(unread_stmt.Get(), 0);
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
        BindText(last_msg_stmt.Get(), 1, peer_id);

        if (sqlite3_step(last_msg_stmt.Get()) == SQLITE_ROW) {
          summary.last_message = ParseHistoryEntry(last_msg_stmt.Get());
          summaries.push_back(summary);
        }
      }
    }

    std::ranges::sort(summaries, [](const auto& a, const auto& b) {
      return a.last_message.timestamp > b.last_message.timestamp;
    });
    return summaries;
  }

  auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> override {
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
    BindText(stmt.Get(), 1, peer_id);
    sqlite3_bind_int(stmt.Get(), 2, static_cast<int>(status));

    while (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      records.push_back(ParseHistoryEntry(stmt.Get()));
    }
    return records;
  }

  // --- Debug ---
  void DebugClearAllHistory() override {
    const std::lock_guard lock(mutex_);
    const char* sql = R"SQL(
      BEGIN TRANSACTION;
      DELETE FROM large_messages;
      DELETE FROM history;
      COMMIT;
    )SQL";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LOGE << "Failed to clear history: " << err_msg;
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  void DebugClearAllContacts() override {
    const std::lock_guard lock(mutex_);
    const char* sql = R"SQL(
      BEGIN TRANSACTION;
      DELETE FROM contacts;
      COMMIT;
    )SQL";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LOGE << "Failed to clear contacts: " << err_msg;
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  void DebugRevokeAllContactApprovals() override {
    const std::lock_guard lock(mutex_);
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LOGE << "Failed to begin transaction for revoking approvals: " << (err_msg ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      return;
    }

    const SQLiteStatement stmt(db_, R"SQL(
      UPDATE
        contacts
      SET
        status = ?
      WHERE
        status = ?;
    )SQL");
    sqlite3_bind_int(stmt.Get(), 1, static_cast<int>(identity::ContactStatus::pendingApproval));
    sqlite3_bind_int(stmt.Get(), 2, static_cast<int>(identity::ContactStatus::confirmed));

    if (sqlite3_step(stmt.Get()) != SQLITE_DONE) {
      LOGE << "Failed to execute revoke contact approvals update: " << sqlite3_errmsg(db_);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    } else {
      if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LOGE << "Failed to commit revoke contact approvals: " << (err_msg ? err_msg : "Unknown error");
        sqlite3_free(err_msg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      }
    }
  }

private:
  void RunMigrations() {
    int user_version = 0;
    const SQLiteStatement get_version_stmt(db_, "PRAGMA user_version;");
    if (sqlite3_step(get_version_stmt.Get()) == SQLITE_ROW) {
      user_version = sqlite3_column_int(get_version_stmt.Get(), 0);
    }

    if (user_version < 1) {
      migrations::migrate_to_v1(db_);
    }
    if (user_version < 2) {
      migrations::migrate_to_v2(db_);
    }
    if (user_version < 3) {
      migrations::migrate_to_v3(db_, MaxInlineContentSize);
    }
    if (user_version < 4) {
      migrations::migrate_to_v4(db_);
    }
    if (user_version < 5) {
      migrations::migrate_to_v5(db_);
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
        record.data =
            messaging::MessageEntry{.content = ColumnString(stmt, 6)};
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
