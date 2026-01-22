#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/migrations.hpp"
#include "eillance/storage/sqlite/sqlite_helpers.hpp"

namespace eillance::storage {

namespace {

constexpr size_t kMaxInlineContentSize = 2048;

auto OptionalHexToBytes(std::optional<std::string> const& hex) -> std::optional<std::vector<std::byte>> {
  if (!hex) {
    return std::nullopt;
  }
  return crypto::FromHex(*hex);
}

auto OptionalBytesToHex(std::optional<std::vector<std::byte>> const& bytes) -> std::optional<std::string> {
  if (!bytes) {
    return std::nullopt;
  }
  return crypto::ToHex(*bytes);
}

}  // namespace

class SQLiteStorage final : public PersistentStorageImpl {
  sqlite3* db_ = nullptr;
  std::recursive_mutex mutex_;
  std::filesystem::path dbPath_;

 public:
  explicit SQLiteStorage(std::filesystem::path const& db_path)
      : dbPath_(db_path) {
    try {
      std::filesystem::create_directories(dbPath_.parent_path());
      if (sqlite3_open(dbPath_.string().c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to open database: ") + sqlite3_errmsg(db_));
      }
      sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
      sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
      RunMigrations();
    } catch (std::exception const& e) {
      LGE << "Failed to initialize SQLite database at " << db_path.string() << ": " << e.what();
      if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
      }
      throw;
    }
  }

  ~SQLiteStorage() override {
    if (db_ != nullptr) {
      sqlite3_close_v2(db_);
    }
  }

  [[nodiscard]] auto GetPath() const -> std::string override { return dbPath_.string(); }

  // --- Settings ---
  auto GetSetting(std::string_view const key) -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
        SELECT
          value
        FROM
          settings
        WHERE
          key = ?;
    )SQL");
    BindText(statement.Get(), 1, std::string(key));
    if (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      return ColumnString(statement.Get(), 0);
    }
    return std::nullopt;
  }

  void SetSetting(std::string_view const key, std::string_view const value) override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      REPLACE INTO settings
        (key, value)
      VALUES
        (?, ?);
    )SQL");
    BindText(statement.Get(), 1, std::string(key));
    BindText(statement.Get(), 2, std::string(value));
    sqlite3_step(statement.Get());
  }

  void DeleteSetting(std::string_view const key) override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      DELETE FROM
        settings
      WHERE
        key = ?;
    )SQL");
    BindText(statement.Get(), 1, std::string(key));
    sqlite3_step(statement.Get());
  }

  // --- Raw Key-Value for backward compatibility if needed ---
  auto GetRaw(std::string const& key) -> std::optional<std::vector<std::byte>> override {
    if (auto const val = GetSetting(key)) {
      return crypto::FromHex(*val);
    }
    return std::nullopt;
  }

  auto SetRaw(std::string const& key, std::vector<std::byte> const& value) -> bool override {
    SetSetting(key, crypto::ToHex(value));
    return true;
  }

  // --- Contacts ---
  auto GetContacts() -> std::vector<identity::Contact> override {
    std::lock_guard const lock(mutex_);
    std::vector<identity::Contact> contacts;
    SQLiteStatement const statement(db_, R"SQL(
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
    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      contacts.push_back(
          {.peer_id = ContactID(ColumnString(statement.Get(), 0)),
           .status = static_cast<identity::ContactStatus>(sqlite3_column_int(statement.Get(), 1)),
           .display_name = ColumnOptionalString(statement.Get(), 2),
           .display_name_override = ColumnOptionalString(statement.Get(), 3),
           .bio = ColumnOptionalString(statement.Get(), 4),
           .photo_base64 = ColumnOptionalString(statement.Get(), 5),
           .signature_from_them = OptionalHexToBytes(ColumnOptionalString(statement.Get(), 6)),
           .my_signature_for_them = OptionalHexToBytes(ColumnOptionalString(statement.Get(), 7))}
      );
    }
    return contacts;
  }

  auto GetContact(ContactID const& peer_id) -> std::optional<identity::Contact> override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
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
    BindText(statement.Get(), 1, peer_id.ToString());
    if (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      return identity::Contact{
          .peer_id = ContactID(ColumnString(statement.Get(), 0)),
          .status = static_cast<identity::ContactStatus>(sqlite3_column_int(statement.Get(), 1)),
          .display_name = ColumnOptionalString(statement.Get(), 2),
          .display_name_override = ColumnOptionalString(statement.Get(), 3),
          .bio = ColumnOptionalString(statement.Get(), 4),
          .photo_base64 = ColumnOptionalString(statement.Get(), 5),
          .signature_from_them = OptionalHexToBytes(ColumnOptionalString(statement.Get(), 6)),
          .my_signature_for_them = OptionalHexToBytes(ColumnOptionalString(statement.Get(), 7))
      };
    }
    return std::nullopt;
  }

  void UpdateContact(identity::Contact const& contact) override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      REPLACE INTO contacts
        (peer_id, status, display_name, display_name_override, bio, photo_base64, signature_from_them, my_signature_for_them)
      VALUES
        (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");
    BindText(statement.Get(), 1, contact.peer_id.ToString());
    sqlite3_bind_int(statement.Get(), 2, static_cast<int>(contact.status));
    BindOptionalText(statement.Get(), 3, contact.display_name);
    BindOptionalText(statement.Get(), 4, contact.display_name_override);
    BindOptionalText(statement.Get(), 5, contact.bio);
    BindOptionalText(statement.Get(), 6, contact.photo_base64);
    BindOptionalText(statement.Get(), 7, OptionalBytesToHex(contact.signature_from_them));
    BindOptionalText(statement.Get(), 8, OptionalBytesToHex(contact.my_signature_for_them));
    sqlite3_step(statement.Get());
  }

  void RemoveContact(ContactID const& peer_id) override {
    std::lock_guard const lock(mutex_);
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to begin transaction for contact removal: " << ((err_msg != nullptr) ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      return;
    }

    try {
      SQLiteStatement const messages_stmt(db_, "DELETE FROM messages WHERE peer_id = ?;");
      BindText(messages_stmt.Get(), 1, peer_id.ToString());
      if (sqlite3_step(messages_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }

      SQLiteStatement const contact_stmt(db_, "DELETE FROM contacts WHERE peer_id = ?;");
      BindText(contact_stmt.Get(), 1, peer_id.ToString());
      if (sqlite3_step(contact_stmt.Get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }
    } catch (std::runtime_error const& e) {
      LGE << "Error during contact removal: " << e.what();
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      return;
    }

    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to commit transaction for contact removal: " << ((err_msg != nullptr) ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  // --- History / Messages ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      INSERT INTO messages (
        peer_id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content
      ) VALUES (?, ?, ?, ?, ?, ?, ?);
    )SQL");

    BindText(statement.Get(), 1, entry.peer_id.ToString());
    sqlite3_bind_int(statement.Get(), 2, static_cast<int>(entry.kind));
    sqlite3_bind_int64(statement.Get(), 3, entry.timestamp);
    sqlite3_bind_int(statement.Get(), 4, static_cast<int>(entry.direction));
    sqlite3_bind_int(statement.Get(), 5, static_cast<int>(entry.status));

    std::optional<std::string> full_content_for_storage;

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      if (msg_entry->content.length() > kMaxInlineContentSize) {
        entry.is_truncated = true;
        full_content_for_storage = msg_entry->content;
        msg_entry->content = full_content_for_storage->substr(0, kMaxInlineContentSize);
      } else {
        entry.is_truncated = false;
      }
      sqlite3_bind_int(statement.Get(), 6, static_cast<int>(entry.is_truncated));
      BindText(statement.Get(), 7, msg_entry->content);
    } else if (auto const* call_entry = std::get_if<messaging::CallEntry>(&entry.data)) {
      entry.is_truncated = false;
      sqlite3_bind_int(statement.Get(), 6, 0);  // is_truncated = false

      nlohmann::json j;
      j["duration_seconds"] = call_entry->duration_seconds;
      j["call_direction"] = call_entry->call_direction;
      j["call_status"] = call_entry->call_status;
      BindText(statement.Get(), 7, j.dump());
    }

    if (sqlite3_step(statement.Get()) != SQLITE_DONE) {
      LGE << "Failed to insert message entry: " << sqlite3_errmsg(db_);
      return {};  // Return empty record on failure
    }

    entry.id = sqlite3_last_insert_rowid(db_);

    if (full_content_for_storage) {
      SQLiteStatement const large_msg_stmt(db_, R"SQL(
        INSERT INTO large_messages
          (message_id, full_content)
        VALUES
          (?, ?);
        )SQL");
      sqlite3_bind_int64(large_msg_stmt.Get(), 1, entry.id);
      BindText(large_msg_stmt.Get(), 2, *full_content_for_storage);
      if (sqlite3_step(large_msg_stmt.Get()) != SQLITE_DONE) {
        LGE << "Failed to insert large message content: " << sqlite3_errmsg(db_);
      }
    }

    return entry;
  }

  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      UPDATE
        messages
      SET
        kind = ?,
        timestamp = ?,
        direction = ?,
        status = ?,
        content = ?
      WHERE
        id = ?;
    )SQL");

    sqlite3_bind_int(statement.Get(), 1, static_cast<int>(entry.kind));
    sqlite3_bind_int64(statement.Get(), 2, entry.timestamp);
    sqlite3_bind_int(statement.Get(), 3, static_cast<int>(entry.direction));
    sqlite3_bind_int(statement.Get(), 4, static_cast<int>(entry.status));

    std::visit(
        [&]<typename T0>(T0&& arg) {
          using T = std::decay_t<T0>;
          if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
            BindText(statement.Get(), 5, arg.content);
          } else if constexpr (std::is_same_v<T, messaging::CallEntry>) {
            nlohmann::json j;
            j["duration_seconds"] = arg.duration_seconds;
            j["call_direction"] = arg.call_direction;
            j["call_status"] = arg.call_status;
            BindText(statement.Get(), 5, j.dump());
          }
        },
        entry.data
    );

    sqlite3_bind_int64(statement.Get(), 6, entry.id);
    sqlite3_step(statement.Get());
  }

  void UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status)
      override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      UPDATE
        messages
      SET
        status = ?
      WHERE
        id = ? AND peer_id = ?;
    )SQL");
    sqlite3_bind_int(statement.Get(), 1, static_cast<int>(new_status));
    sqlite3_bind_int64(statement.Get(), 2, message_id);
    BindText(statement.Get(), 3, peer_id.ToString());
    sqlite3_step(statement.Get());
  }

  void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) override {
    if (message_ids.empty()) {
      return;
    }
    std::lock_guard const lock(mutex_);
    nlohmann::json const ids_json = message_ids;
    std::string const query = R"SQL(
      UPDATE
        messages
      SET
        status = ?
      WHERE
        peer_id = ? AND id IN (SELECT value FROM json_each(?));
    )SQL";

    SQLiteStatement const statement(db_, query);
    sqlite3_bind_int(statement.Get(), 1, static_cast<int>(new_status));
    BindText(statement.Get(), 2, peer_id.ToString());
    BindText(statement.Get(), 3, ids_json.dump());
    sqlite3_step(statement.Get());
  }

  auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage override {
    std::lock_guard const lock(mutex_);
    messaging::HistoryPage page;

    {
      SQLiteStatement const count_stmt(db_, R"SQL(
        SELECT
          count(*)
        FROM
          messages
        WHERE
          peer_id = ?;
      )SQL");
      BindText(count_stmt.Get(), 1, peer_id.ToString());
      if (sqlite3_step(count_stmt.Get()) == SQLITE_ROW) {
        page.total_messages = sqlite3_column_int64(count_stmt.Get(), 0);
      }
    }

    SQLiteStatement const statement(db_, R"SQL(
      SELECT
        id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content
      FROM
        messages
      WHERE
        peer_id = ?
      ORDER BY
        timestamp DESC
      LIMIT ? OFFSET ?;
    )SQL");
    BindText(statement.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(statement.Get(), 2, page_size);
    sqlite3_bind_int64(statement.Get(), 3, page_index * page_size);

    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = peer_id;
      page.messages.push_back(std::move(record));
    }
    std::ranges::reverse(page.messages);  // Reverse to get ascending time order for display
    return page;
  }

  auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    SQLiteStatement const statement(db_, R"SQL(
      SELECT
        full_content
      FROM
        large_messages
      WHERE
        message_id = ?;
    )SQL");
    sqlite3_bind_int64(statement.Get(), 1, message_id);
    if (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      return ColumnString(statement.Get(), 0);
    }
    return std::nullopt;
  }

  auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> override {
    if (peer_ids.empty()) {
      return {};
    }
    std::lock_guard const lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (auto const& peer_id : peer_ids) {
      messaging::ChatSummary summary;
      summary.peer_id = peer_id;
      summary.unread_count = 0;

      // Query 1: Get unread count
      {
        SQLiteStatement const unread_stmt(db_, R"SQL(
          SELECT
            COUNT(*)
          FROM
            messages
          WHERE
            peer_id = ?
            AND direction = ?
            AND status != ?
            AND kind = ?;
        )SQL");

        int bind_idx = 1;
        BindText(unread_stmt.Get(), bind_idx++, peer_id.ToString());
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++, static_cast<int>(messaging::MessageDirection::received));
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++, static_cast<int>(messaging::MessageStatus::read));
        sqlite3_bind_int(unread_stmt.Get(), bind_idx++, static_cast<int>(messaging::HistoryEntryKind::textMessage));

        if (sqlite3_step(unread_stmt.Get()) == SQLITE_ROW) {
          summary.unread_count = sqlite3_column_int(unread_stmt.Get(), 0);
        }
      }

      // Query 2: Get last message
      {
        SQLiteStatement const last_msg_stmt(db_, R"SQL(
          SELECT
            id,
            kind,
            timestamp,
            direction,
            status,
            is_truncated,
            content
          FROM
            messages
          WHERE
            peer_id = ?
          ORDER BY
            timestamp DESC
          LIMIT 1;
        )SQL");
        BindText(last_msg_stmt.Get(), 1, peer_id.ToString());

        if (sqlite3_step(last_msg_stmt.Get()) == SQLITE_ROW) {
          summary.last_message = ParseHistoryEntry(last_msg_stmt.Get());
          summary.last_message.peer_id = peer_id;
          summaries.push_back(summary);
        }
      }
    }

    std::ranges::sort(summaries, [](auto const& a, auto const& b) {
      return a.last_message.timestamp > b.last_message.timestamp;
    });
    return summaries;
  }

  auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> records;
    SQLiteStatement const statement(db_, R"SQL(
      SELECT
        id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content
      FROM
        messages
      WHERE
        peer_id = ? AND status = ?;
    )SQL");
    BindText(statement.Get(), 1, peer_id.ToString());
    sqlite3_bind_int(statement.Get(), 2, static_cast<int>(status));

    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = peer_id;
      records.push_back(std::move(record));
    }
    return records;
  }

  // --- Debug ---
  void DebugClearAllHistory() override {
    std::lock_guard const lock(mutex_);
    auto const* sql = R"SQL(
      BEGIN TRANSACTION;
      DELETE FROM large_messages;
      DELETE FROM messages;
      COMMIT;
    )SQL";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to clear history: " << err_msg;
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  void DebugClearAllContacts() override {
    std::lock_guard const lock(mutex_);
    auto const* sql = R"SQL(
      BEGIN TRANSACTION;
      DELETE FROM contacts;
      COMMIT;
    )SQL";
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to clear contacts: " << err_msg;
      sqlite3_free(err_msg);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
  }

  void DebugRevokeAllContactApprovals() override {
    std::lock_guard const lock(mutex_);
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to begin transaction for revoking approvals: "
          << ((err_msg != nullptr) ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      return;
    }

    SQLiteStatement const statement(db_, R"SQL(
      UPDATE
        contacts
      SET
        status = ?
      WHERE
        status = ?;
    )SQL");
    sqlite3_bind_int(statement.Get(), 1, static_cast<int>(identity::ContactStatus::pendingApproval));
    sqlite3_bind_int(statement.Get(), 2, static_cast<int>(identity::ContactStatus::confirmed));

    if (sqlite3_step(statement.Get()) != SQLITE_DONE) {
      LGE << "Failed to execute revoke contact approvals update: " << sqlite3_errmsg(db_);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    } else {
      if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        LGE << "Failed to commit revoke contact approvals: " << ((err_msg != nullptr) ? err_msg : "Unknown error");
        sqlite3_free(err_msg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      }
    }
  }

 private:
  void RunMigrations() const {
    int user_version = 0;
    SQLiteStatement const get_version_stmt(db_, "PRAGMA user_version;");
    if (sqlite3_step(get_version_stmt.Get()) == SQLITE_ROW) {
      user_version = sqlite3_column_int(get_version_stmt.Get(), 0);
    }

    if (user_version == 0) {
      migrations::MigrateToV0001(db_);
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
        record.data = messaging::MessageEntry{.content = ColumnString(stmt, 6)};
        break;
      case messaging::HistoryEntryKind::voiceCall:
      case messaging::HistoryEntryKind::videoCall: {
        auto content_json_str = ColumnString(stmt, 6);
        messaging::CallEntry call_entry{};
        if (!content_json_str.empty()) {
          try {
            auto j = nlohmann::json::parse(content_json_str);
            call_entry.duration_seconds = j.value("duration_seconds", 0U);
            call_entry.call_direction = j.value("call_direction", messaging::CallDirection::outgoing);
            call_entry.call_status = j.value("call_status", messaging::CallStatus::answered);
          } catch (std::exception const& e) {
            LGE << "Failed to parse call entry JSON: " << e.what();
          }
        }
        record.data = call_entry;
        break;
      }
    }
    return record;
  }
};

}  // namespace eillance::storage
