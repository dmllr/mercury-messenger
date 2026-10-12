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
#include "eillance/utils/invariant.hpp"

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

void RegisterCustomLower(sqlite3* db) {
  sqlite3_create_function(
      db,
      "lower",
      1,
      SQLITE_UTF8 | SQLITE_DETERMINISTIC,
      nullptr,
      [](sqlite3_context* ctx, int argc, sqlite3_value** argv) -> void {
        if (argc == 0 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
          sqlite3_result_null(ctx);
          return;
        }
        auto const* text = reinterpret_cast<unsigned char const*>(sqlite3_value_text(argv[0]));
        int const len = sqlite3_value_bytes(argv[0]);

        std::string out;
        out.reserve(len);

        for (int i = 0; i < len;) {
          unsigned char c = text[i];
          if (c < 0x80) {
            // ASCII
            if (c >= 'A' && c <= 'Z') {
              out.push_back(static_cast<char>(c + 32));
            } else {
              out.push_back(static_cast<char>(c));
            }
            i++;
          } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence (Latin-1, Cyrillic, Greek, etc.)
            if (i + 1 >= len) {
              out.push_back(static_cast<char>(c));
              i++;
              continue;
            }
            uint32_t wc = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);

            // Basic Case Folding for common ranges
            // Latin-1 Supplement (U+00C0..U+00DE) -> +0x20, exclude multiplication sign 0xD7
            if (wc >= 0xC0 && wc <= 0xDE && wc != 0xD7) wc += 0x20;
            // Cyrillic (U+0410..U+042F) -> +0x20
            else if (wc >= 0x410 && wc <= 0x42F)
              wc += 0x20;
            // Greek (U+0391..U+03A9) -> +0x20
            else if (wc >= 0x391 && wc <= 0x3A9)
              wc += 0x20;

            out.push_back(static_cast<char>(0xC0 | (wc >> 6)));
            out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
            i += 2;
          } else {
            // 3 or 4 byte sequences (Asian scripts, Emoji, etc. - mostly caseless)
            // Just copy them
            int seq_len = 1;
            if ((c & 0xF0) == 0xE0) {
              seq_len = 3;
            } else if ((c & 0xF8) == 0xF0) {
              seq_len = 4;
            }

            for (int k = 0; k < seq_len && i < len; ++k) {
              out.push_back(static_cast<char>(text[i++]));
            }
          }
        }
        sqlite3_result_text(ctx, out.c_str(), static_cast<int>(out.size()), SQLITE_TRANSIENT);
      },
      nullptr,
      nullptr
  );
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
      RegisterCustomLower(db_);
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

  [[nodiscard]] auto GetPath() const -> std::string override {
    return dbPath_.string();
  }

  // --- Lifecycle ---

  void Close() override {
    std::lock_guard const lock(mutex_);
    if (db_ != nullptr) {
      sqlite3_close_v2(db_);
      db_ = nullptr;
    }
  }

  auto Open() -> bool override {
    std::lock_guard const lock(mutex_);
    if (db_ != nullptr) {
      return true;
    }
    try {
      if (sqlite3_open(dbPath_.string().c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(
            std::string("Failed to open database: ") + ((db_ != nullptr) ? sqlite3_errmsg(db_) : "Unknown error")
        );
      }
      RegisterCustomLower(db_);
      sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
      sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
      RunMigrations();
      return true;
    } catch (std::exception const& e) {
      LGE << "Failed to re-open database: " << e.what();
      if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
      }
      return false;
    }
  }

  void Checkpoint() override {
    std::lock_guard const lock(mutex_);
    if (db_ != nullptr) {
      if (sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr) != SQLITE_OK) {
        LGE << "Failed to checkpoint database: " << sqlite3_errmsg(db_);
      }
    }
  }

  // --- Settings ---
  auto GetSetting(std::string_view const key) -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const statement(db_, "SELECT value FROM settings WHERE key = ?;");
    BindText(statement.Get(), 1, std::string(key));
    if (sqlite3_step(statement.Get()) == SQLITE_ROW) return ColumnString(statement.Get(), 0);
    return std::nullopt;
  }

  void SetSetting(std::string_view const key, std::string_view const value) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const statement(db_, "REPLACE INTO settings (key, value) VALUES (?, ?);");
    BindText(statement.Get(), 1, std::string(key));
    BindText(statement.Get(), 2, std::string(value));
    sqlite3_step(statement.Get());
  }

  void DeleteSetting(std::string_view const key) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const statement(db_, "DELETE FROM settings WHERE key = ?;");
    BindText(statement.Get(), 1, std::string(key));
    sqlite3_step(statement.Get());
  }

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
    INVARIANT(db_);
    std::vector<identity::Contact> contacts;
    SQLiteStatement const statement(
        db_,
        "SELECT peer_id, status, display_name, display_name_override, bio, photo_base64, signature_from_them, "
        "my_signature_for_them FROM contacts;"
    );
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
    INVARIANT(db_);
    SQLiteStatement const statement(
        db_,
        "SELECT peer_id, status, display_name, display_name_override, bio, photo_base64, signature_from_them, "
        "my_signature_for_them FROM contacts WHERE peer_id = ?;"
    );
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
    INVARIANT(db_);
    SQLiteStatement const statement(
        db_,
        "REPLACE INTO contacts (peer_id, status, display_name, display_name_override, bio, photo_base64, "
        "signature_from_them, my_signature_for_them) VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
    );
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
    INVARIANT(db_);
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    SQLiteStatement const m_stmt(db_, "DELETE FROM messages WHERE peer_id = ?;");
    BindText(m_stmt.Get(), 1, peer_id.ToString());
    sqlite3_step(m_stmt.Get());
    SQLiteStatement const l_stmt(db_, "DELETE FROM ledger_entries WHERE topic_id = ?;");
    BindText(l_stmt.Get(), 1, peer_id.ToString());
    sqlite3_step(l_stmt.Get());
    SQLiteStatement const s_stmt(db_, "DELETE FROM sync_requests WHERE topic_id = ?;");
    BindText(s_stmt.Get(), 1, peer_id.ToString());
    sqlite3_step(s_stmt.Get());
    SQLiteStatement const c_stmt(db_, "DELETE FROM contacts WHERE peer_id = ?;");
    BindText(c_stmt.Get(), 1, peer_id.ToString());
    sqlite3_step(c_stmt.Get());
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  }

  // --- Synchronization Ledger ---
  void AddLedgerEntry(messaging::LedgerEntry const& entry) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "INSERT INTO ledger_entries (topic_id, author_id, created_at, type, payload, prev_hash, hash) VALUES (?, ?, ?, "
        "?, ?, ?, ?);"
    );
    BindText(s.Get(), 1, entry.topic_id.ToString());
    BindText(s.Get(), 2, entry.author_id.ToString());
    sqlite3_bind_int64(s.Get(), 3, entry.created_at.value);
    sqlite3_bind_int(s.Get(), 4, static_cast<int>(entry.type));
    BindText(s.Get(), 5, entry.payload);
    BindText(s.Get(), 6, entry.prev_hash.ToString());
    BindText(s.Get(), 7, entry.hash.ToString());
    sqlite3_step(s.Get());
  }

  auto GetLedgerEntryByHash(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::optional<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "SELECT seq_id, topic_id, author_id, created_at, type, payload, prev_hash, hash FROM ledger_entries WHERE "
        "topic_id = ? AND hash = ? LIMIT 1;"
    );
    BindText(s.Get(), 1, topic_id.ToString());
    BindText(s.Get(), 2, hash.ToString());
    if (sqlite3_step(s.Get()) == SQLITE_ROW) {
      return messaging::LedgerEntry{
          static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 0)),
          TopicID(ColumnString(s.Get(), 1)),
          ContactID(ColumnString(s.Get(), 2)),
          Timestamp{static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 3))},
          static_cast<messaging::LedgerActionType>(sqlite3_column_int(s.Get(), 4)),
          ColumnString(s.Get(), 5),
          messaging::LedgerHash(ColumnString(s.Get(), 6)),
          messaging::LedgerHash(ColumnString(s.Get(), 7))
      };
    }
    return std::nullopt;
  }

  auto GetLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::vector<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::LedgerEntry> entries;

    uint64_t start_seq = 0;
    if (!hash.IsEmpty()) {
      SQLiteStatement const s_seq(db_, "SELECT seq_id FROM ledger_entries WHERE topic_id = ? AND hash = ?;");
      BindText(s_seq.Get(), 1, topic_id.ToString());
      BindText(s_seq.Get(), 2, hash.ToString());
      if (sqlite3_step(s_seq.Get()) == SQLITE_ROW) {
        start_seq = sqlite3_column_int64(s_seq.Get(), 0);
      } else {
        return entries;  // LCA not found, return empty indicating full rebuild needed
      }
    }

    SQLiteStatement const s(
        db_,
        "SELECT seq_id, topic_id, author_id, created_at, type, payload, prev_hash, hash FROM ledger_entries WHERE "
        "topic_id = ? AND seq_id > ? ORDER BY seq_id ASC;"
    );
    BindText(s.Get(), 1, topic_id.ToString());
    sqlite3_bind_int64(s.Get(), 2, start_seq);
    while (sqlite3_step(s.Get()) == SQLITE_ROW) {
      entries.push_back(
          {static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 0)),
           TopicID(ColumnString(s.Get(), 1)),
           ContactID(ColumnString(s.Get(), 2)),
           Timestamp{static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 3))},
           static_cast<messaging::LedgerActionType>(sqlite3_column_int(s.Get(), 4)),
           ColumnString(s.Get(), 5),
           messaging::LedgerHash(ColumnString(s.Get(), 6)),
           messaging::LedgerHash(ColumnString(s.Get(), 7))}
      );
    }
    return entries;
  }

  auto GetLedgerEntries(TopicID const& topic_id) -> std::vector<messaging::LedgerEntry> override {
    return GetLedgerEntriesAfter(topic_id, messaging::LedgerHash());
  }

  auto GetLatestLedgerEntry(TopicID const& topic_id) -> std::optional<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "SELECT seq_id, topic_id, author_id, created_at, type, payload, prev_hash, hash FROM ledger_entries WHERE "
        "topic_id = ? ORDER BY seq_id DESC LIMIT 1;"
    );
    BindText(s.Get(), 1, topic_id.ToString());
    if (sqlite3_step(s.Get()) == SQLITE_ROW) {
      return messaging::LedgerEntry{
          static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 0)),
          TopicID(ColumnString(s.Get(), 1)),
          ContactID(ColumnString(s.Get(), 2)),
          Timestamp{static_cast<uint64_t>(sqlite3_column_int64(s.Get(), 3))},
          static_cast<messaging::LedgerActionType>(sqlite3_column_int(s.Get(), 4)),
          ColumnString(s.Get(), 5),
          messaging::LedgerHash(ColumnString(s.Get(), 6)),
          messaging::LedgerHash(ColumnString(s.Get(), 7))
      };
    }
    return std::nullopt;
  }

  void DeleteLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    if (hash.IsEmpty()) {
      SQLiteStatement const s_del(db_, "DELETE FROM ledger_entries WHERE topic_id = ?;");
      BindText(s_del.Get(), 1, topic_id.ToString());
      sqlite3_step(s_del.Get());
      return;
    }

    SQLiteStatement const s_seq(db_, "SELECT seq_id FROM ledger_entries WHERE topic_id = ? AND hash = ?;");
    BindText(s_seq.Get(), 1, topic_id.ToString());
    BindText(s_seq.Get(), 2, hash.ToString());
    if (sqlite3_step(s_seq.Get()) == SQLITE_ROW) {
      uint64_t const start_seq = sqlite3_column_int64(s_seq.Get(), 0);
      SQLiteStatement const s_del(db_, "DELETE FROM ledger_entries WHERE topic_id = ? AND seq_id > ?;");
      BindText(s_del.Get(), 1, topic_id.ToString());
      sqlite3_bind_int64(s_del.Get(), 2, start_seq);
      sqlite3_step(s_del.Get());
    }
  }

  auto GetRecentLedgerHashes(TopicID const& topic_id) -> std::vector<messaging::LedgerHash> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::LedgerHash> hashes;

    SQLiteStatement const s(db_, "SELECT hash FROM ledger_entries WHERE topic_id = ? ORDER BY seq_id DESC;");
    BindText(s.Get(), 1, topic_id.ToString());

    std::vector<messaging::LedgerHash> all_hashes;
    while (sqlite3_step(s.Get()) == SQLITE_ROW) {
      all_hashes.push_back(messaging::LedgerHash(ColumnString(s.Get(), 0)));
    }

    if (all_hashes.empty()) {
      return {messaging::LedgerHash()};
    }

    size_t step = 1;
    for (size_t i = 0; i < all_hashes.size();) {
      hashes.push_back(all_hashes[i]);
      if (i == all_hashes.size() - 1) break;
      i += step;
      if (i >= all_hashes.size()) i = all_hashes.size() - 1;
      if (step < 1000) step *= 2;
    }

    if (!hashes.back().IsEmpty()) {
      hashes.push_back(messaging::LedgerHash());
    }

    return hashes;
  }

  auto HasLedgerEntry(TopicID const& topic_id, messaging::LedgerHash const& hash) -> bool override {
    if (hash.IsEmpty()) return true;
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(db_, "SELECT 1 FROM ledger_entries WHERE topic_id = ? AND hash = ? LIMIT 1;");
    BindText(s.Get(), 1, topic_id.ToString());
    BindText(s.Get(), 2, hash.ToString());
    return sqlite3_step(s.Get()) == SQLITE_ROW;
  }

  void ReplaceLedgerEntries(
      TopicID const& topic_id,
      messaging::LedgerHash const& lca_hash,
      std::vector<messaging::LedgerEntry> const& entries
  ) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    if (lca_hash.IsEmpty()) {
      SQLiteStatement const s_del(db_, "DELETE FROM ledger_entries WHERE topic_id = ?;");
      BindText(s_del.Get(), 1, topic_id.ToString());
      sqlite3_step(s_del.Get());
    } else {
      SQLiteStatement const s_seq(db_, "SELECT seq_id FROM ledger_entries WHERE topic_id = ? AND hash = ?;");
      BindText(s_seq.Get(), 1, topic_id.ToString());
      BindText(s_seq.Get(), 2, lca_hash.ToString());
      if (sqlite3_step(s_seq.Get()) == SQLITE_ROW) {
        uint64_t const start_seq = sqlite3_column_int64(s_seq.Get(), 0);
        SQLiteStatement const s_del(db_, "DELETE FROM ledger_entries WHERE topic_id = ? AND seq_id > ?;");
        BindText(s_del.Get(), 1, topic_id.ToString());
        sqlite3_bind_int64(s_del.Get(), 2, start_seq);
        sqlite3_step(s_del.Get());
      }
    }

    SQLiteStatement const s(
        db_,
        "INSERT OR REPLACE INTO ledger_entries (topic_id, author_id, created_at, type, payload, prev_hash, hash) "
        "VALUES (?, ?, ?, "
        "?, ?, ?, ?);"
    );
    for (auto const& entry : entries) {
      sqlite3_reset(s.Get());
      sqlite3_clear_bindings(s.Get());
      BindText(s.Get(), 1, entry.topic_id.ToString());
      BindText(s.Get(), 2, entry.author_id.ToString());
      sqlite3_bind_int64(s.Get(), 3, entry.created_at.value);
      sqlite3_bind_int(s.Get(), 4, static_cast<int>(entry.type));
      BindText(s.Get(), 5, entry.payload);
      BindText(s.Get(), 6, entry.prev_hash.ToString());
      BindText(s.Get(), 7, entry.hash.ToString());
      sqlite3_step(s.Get());
    }

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  }

  // --- Sync Requests ---
  void AddSyncRequest(messaging::SyncRequest const& req) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "REPLACE INTO sync_requests (topic_id, target_peer_id, created_at, last_attempt) VALUES (?, ?, ?, ?);"
    );
    BindText(s.Get(), 1, req.topic_id.ToString());
    BindText(s.Get(), 2, req.target_peer_id.ToString());
    sqlite3_bind_int64(s.Get(), 3, req.created_at.value);
    sqlite3_bind_int64(s.Get(), 4, req.last_attempt.value);
    sqlite3_step(s.Get());
  }

  auto GetSyncRequests() -> std::vector<messaging::SyncRequest> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::SyncRequest> reqs;
    SQLiteStatement const statement(
        db_,
        "SELECT topic_id, target_peer_id, created_at, last_attempt FROM sync_requests ORDER BY created_at ASC;"
    );
    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      reqs.push_back(
          {TopicID(ColumnString(statement.Get(), 0)),
           ContactID(ColumnString(statement.Get(), 1)),
           Timestamp{static_cast<uint64_t>(sqlite3_column_int64(statement.Get(), 2))},
           Timestamp{static_cast<uint64_t>(sqlite3_column_int64(statement.Get(), 3))}}
      );
    }
    return reqs;
  }

  void RemoveSyncRequest(TopicID const& topic_id, ContactID const& target_peer_id) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const statement(db_, "DELETE FROM sync_requests WHERE topic_id = ? AND target_peer_id = ?;");
    BindText(statement.Get(), 1, topic_id.ToString());
    BindText(statement.Get(), 2, target_peer_id.ToString());
    sqlite3_step(statement.Get());
  }

  void
  UpdateSyncRequestAttempt(TopicID const& topic_id, ContactID const& target_peer_id, Timestamp attempt_time) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const statement(
        db_,
        "UPDATE sync_requests SET last_attempt = ? WHERE topic_id = ? AND target_peer_id = ?;"
    );
    sqlite3_bind_int64(statement.Get(), 1, static_cast<sqlite3_int64>(attempt_time.value));
    BindText(statement.Get(), 2, topic_id.ToString());
    BindText(statement.Get(), 3, target_peer_id.ToString());
    sqlite3_step(statement.Get());
  }

  // --- History / Messages ---
  void ClearTopicReadModel(TopicID const& topic_id) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    SQLiteStatement const s1(db_, "DELETE FROM messages WHERE peer_id = ?;");
    BindText(s1.Get(), 1, topic_id.ToString());
    sqlite3_step(s1.Get());
    SQLiteStatement const s2(db_, "DELETE FROM reactions WHERE peer_id = ?;");
    BindText(s2.Get(), 1, topic_id.ToString());
    sqlite3_step(s2.Get());
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  }

  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
      LGE << "Failed to begin transaction for message insertion: "
          << ((err_msg != nullptr) ? err_msg : "Unknown error");
      sqlite3_free(err_msg);
      return {};
    }

    INVARIANT(entry.id != 0);

    SQLiteStatement const statement(db_, R"SQL(
      INSERT INTO messages (
        peer_id,
        id,
        kind,
        timestamp,
        direction,
        status,
        is_truncated,
        content,
        reply_to_id
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    BindText(statement.Get(), 1, entry.peer_id.ToString());
    sqlite3_bind_int64(statement.Get(), 2, entry.id);
    sqlite3_bind_int(statement.Get(), 3, static_cast<int>(entry.kind));
    sqlite3_bind_int64(statement.Get(), 4, entry.timestamp);
    sqlite3_bind_int(statement.Get(), 5, static_cast<int>(entry.direction));
    sqlite3_bind_int(statement.Get(), 6, static_cast<int>(entry.status));

    std::optional<std::string> full_content_for_storage;

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      if (msg_entry->content.length() > kMaxInlineContentSize) {
        entry.is_truncated = true;
        full_content_for_storage = msg_entry->content;
        msg_entry->content = full_content_for_storage->substr(0, kMaxInlineContentSize);
      } else {
        entry.is_truncated = false;
      }
      sqlite3_bind_int(statement.Get(), 7, static_cast<int>(entry.is_truncated));
      BindText(statement.Get(), 8, msg_entry->content);
      sqlite3_bind_int64(statement.Get(), 9, msg_entry->reply_to_id);
    } else if (auto const* call_entry = std::get_if<messaging::CallEntry>(&entry.data)) {
      entry.is_truncated = false;
      sqlite3_bind_int(statement.Get(), 7, 0);  // is_truncated = false

      nlohmann::json j;
      j["duration_seconds"] = call_entry->duration_seconds;
      j["call_direction"] = call_entry->call_direction;
      j["call_status"] = call_entry->call_status;
      BindText(statement.Get(), 8, j.dump());
      sqlite3_bind_int64(statement.Get(), 9, 0);
    }

    if (sqlite3_step(statement.Get()) != SQLITE_DONE) {
      LGE << "Failed to insert message entry: " << sqlite3_errmsg(db_);
      sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
      return {};
    }

    if (full_content_for_storage) {
      SQLiteStatement const large_msg_stmt(db_, R"SQL(
        INSERT INTO large_messages
          (peer_id, message_id, full_content)
        VALUES
          (?, ?, ?);
        )SQL");
      BindText(large_msg_stmt.Get(), 1, entry.peer_id.ToString());
      sqlite3_bind_int64(large_msg_stmt.Get(), 2, entry.id);
      BindText(large_msg_stmt.Get(), 3, *full_content_for_storage);
      if (sqlite3_step(large_msg_stmt.Get()) != SQLITE_DONE) {
        LGE << "Failed to insert large message content: " << sqlite3_errmsg(db_);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return {};
      }
    }

    // 3. Insert attachments if present
    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      for (auto const& att : msg_entry->attachments) {
        // v6 update: added `file_name`
        SQLiteStatement const att_stmt(db_, R"SQL(
          INSERT INTO message_attachments (
            id, peer_id, message_id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )SQL");
        BindText(att_stmt.Get(), 1, att.id);
        BindText(att_stmt.Get(), 2, entry.peer_id.ToString());
        sqlite3_bind_int64(att_stmt.Get(), 3, entry.id);
        sqlite3_bind_int(att_stmt.Get(), 4, static_cast<int>(att.type));
        BindText(att_stmt.Get(), 5, att.file_path);
        BindText(att_stmt.Get(), 6, att.mime_type);
        sqlite3_bind_int64(att_stmt.Get(), 7, att.size_bytes);
        BindText(att_stmt.Get(), 8, att.thumbnail_base64);
        BindText(att_stmt.Get(), 9, att.hash);
        sqlite3_bind_int(att_stmt.Get(), 10, att.is_downloaded ? 1 : 0);
        sqlite3_bind_int(att_stmt.Get(), 11, static_cast<int>(att.status));
        BindText(att_stmt.Get(), 12, att.note);
        BindText(att_stmt.Get(), 13, att.file_name);

        if (sqlite3_step(att_stmt.Get()) != SQLITE_DONE) {
          LGE << "Failed to insert attachment: " << sqlite3_errmsg(db_);
          sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
          return {};
        }
      }
    }

    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return entry;
  }

  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    SQLiteStatement const statement(
        db_,
        "UPDATE messages SET kind=?, timestamp=?, direction=?, status=?, content=? WHERE peer_id=? AND id=?;"
    );
    sqlite3_bind_int(statement.Get(), 1, static_cast<int>(entry.kind));
    sqlite3_bind_int64(statement.Get(), 2, entry.timestamp);
    sqlite3_bind_int(statement.Get(), 3, static_cast<int>(entry.direction));
    sqlite3_bind_int(statement.Get(), 4, static_cast<int>(entry.status));
    if (auto* msg = std::get_if<messaging::MessageEntry>(&entry.data)) {
      BindText(statement.Get(), 5, msg->content);
    } else {
      BindText(statement.Get(), 5, "");
    }
    BindText(statement.Get(), 6, entry.peer_id.ToString());
    sqlite3_bind_int64(statement.Get(), 7, entry.id);
    sqlite3_step(statement.Get());

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      for (auto const& att : msg_entry->attachments) {
        SQLiteStatement const att_update(
            db_,
            "UPDATE message_attachments SET is_downloaded = ?, status = ? WHERE id = ?;"
        );
        sqlite3_bind_int(att_update.Get(), 1, att.is_downloaded ? 1 : 0);
        sqlite3_bind_int(att_update.Get(), 2, static_cast<int>(att.status));
        BindText(att_update.Get(), 3, att.id);
        sqlite3_step(att_update.Get());
      }
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
  }

  void
  UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(db_, "UPDATE messages SET status=? WHERE peer_id=? AND id=?;");
    sqlite3_bind_int(s.Get(), 1, static_cast<int>(new_status));
    BindText(s.Get(), 2, peer_id.ToString());
    sqlite3_bind_int64(s.Get(), 3, message_id);
    sqlite3_step(s.Get());
  }

  void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) override {
    if (message_ids.empty()) return;
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    nlohmann::json const ids_json = message_ids;
    SQLiteStatement const s(
        db_,
        "UPDATE messages SET status=? WHERE peer_id=? AND id IN (SELECT value FROM json_each(?));"
    );
    sqlite3_bind_int(s.Get(), 1, static_cast<int>(new_status));
    BindText(s.Get(), 2, peer_id.ToString());
    BindText(s.Get(), 3, ids_json.dump());
    sqlite3_step(s.Get());
  }

  void UpdateMessageContent(ContactID const& peer_id, uint64_t message_id, std::string const& new_content) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "UPDATE messages SET content=?, is_edited=1, is_truncated=0 WHERE peer_id=? AND id=?;"
    );
    BindText(s.Get(), 1, new_content);
    BindText(s.Get(), 2, peer_id.ToString());
    sqlite3_bind_int64(s.Get(), 3, message_id);
    sqlite3_step(s.Get());

    // Clear large message record if it exists, as we assume edit fits in inline for simplicity (or should handle large
    // logic) For robustness: we just delete the large entry. If the new content is huge, we should logic like
    // AddHistoryEntry. But here we rely on the caller to not send >2KB edits via this path or handle truncation.
    SQLiteStatement const del_large(db_, "DELETE FROM large_messages WHERE peer_id=? AND message_id=?;");
    BindText(del_large.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(del_large.Get(), 2, message_id);
    sqlite3_step(del_large.Get());
  }

  void MarkMessageDeleted(ContactID const& peer_id, uint64_t message_id) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(
        db_,
        "UPDATE messages SET is_deleted=1, content='', is_truncated=0 WHERE peer_id=? AND id=?;"
    );
    BindText(s.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(s.Get(), 2, message_id);
    sqlite3_step(s.Get());

    // Clear content details
    SQLiteStatement const del_large(db_, "DELETE FROM large_messages WHERE peer_id=? AND message_id=?;");
    BindText(del_large.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(del_large.Get(), 2, message_id);
    sqlite3_step(del_large.Get());

    SQLiteStatement const del_atts(db_, "DELETE FROM message_attachments WHERE peer_id=? AND message_id=?;");
    BindText(del_atts.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(del_atts.Get(), 2, message_id);
    sqlite3_step(del_atts.Get());
  }

  void ToggleReaction(
      ContactID const& peer_id,
      uint64_t message_id,
      ContactID const& author_id,
      messaging::Reaction reaction
  ) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    // Check existence
    SQLiteStatement const check(
        db_,
        "SELECT 1 FROM reactions WHERE peer_id=? AND message_id=? AND author_id=? AND reaction=?;"
    );
    BindText(check.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(check.Get(), 2, message_id);
    BindText(check.Get(), 3, author_id.ToString());
    sqlite3_bind_int(check.Get(), 4, static_cast<int>(reaction));

    bool const exists = (sqlite3_step(check.Get()) == SQLITE_ROW);

    std::string const sql =
        exists ? "DELETE FROM reactions WHERE peer_id=? AND message_id=? AND author_id=? AND reaction=?;"
               : "INSERT INTO reactions (peer_id, message_id, author_id, reaction) VALUES (?, ?, ?, ?);";

    SQLiteStatement const op(db_, sql);
    BindText(op.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(op.Get(), 2, message_id);
    BindText(op.Get(), 3, author_id.ToString());
    sqlite3_bind_int(op.Get(), 4, static_cast<int>(reaction));
    sqlite3_step(op.Get());
  }

  void UpdateAttachmentStatus(
      std::string const& attachment_id,
      messaging::AttachmentStatus status,
      bool is_downloaded
  ) override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(db_, "UPDATE message_attachments SET status=?, is_downloaded=? WHERE id=?;");
    sqlite3_bind_int(s.Get(), 1, static_cast<int>(status));
    sqlite3_bind_int(s.Get(), 2, is_downloaded ? 1 : 0);
    BindText(s.Get(), 3, attachment_id);
    sqlite3_step(s.Get());
  }

  auto GetAttachment(std::string const& attachment_id) -> std::optional<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    // v6 update: select `file_name`
    SQLiteStatement const statement(db_, R"SQL(
      SELECT
        id,
        type,
        file_path,
        mime_type,
        size_bytes,
        thumbnail_base64,
        hash,
        is_downloaded,
        status,
        note,
        file_name
      FROM
        message_attachments
      WHERE
        id = ?;
    )SQL");
    BindText(statement.Get(), 1, attachment_id);

    if (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      return messaging::Attachment{
          .id = ColumnString(statement.Get(), 0),
          .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(statement.Get(), 1)),
          .file_path = ColumnString(statement.Get(), 2),
          .mime_type = ColumnString(statement.Get(), 3),
          .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(statement.Get(), 4)),
          .thumbnail_base64 = ColumnString(statement.Get(), 5),
          .hash = ColumnString(statement.Get(), 6),
          .is_downloaded = (sqlite3_column_int(statement.Get(), 7) != 0),
          .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(statement.Get(), 8)),
          .note = ColumnString(statement.Get(), 9),
          .file_name = ColumnString(statement.Get(), 10)
      };
    }
    return std::nullopt;
  }

  auto GetDownloadedAttachments(ContactID const& peer_id) -> std::vector<messaging::AttachmentRecord> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::AttachmentRecord> results;
    SQLiteStatement const stmt(db_, R"SQL(
      SELECT a.id, a.type, a.file_path, a.mime_type, a.size_bytes, a.thumbnail_base64, a.hash, a.is_downloaded, a.status, a.note, a.file_name, m.timestamp
      FROM message_attachments a
      JOIN messages m ON a.message_id = m.id AND a.peer_id = m.peer_id
      WHERE a.peer_id = ? AND a.is_downloaded = 1 AND a.status = 2
      ORDER BY m.timestamp DESC;
    )SQL");
    BindText(stmt.Get(), 1, peer_id.ToString());
    while (sqlite3_step(stmt.Get()) == SQLITE_ROW) {
      messaging::Attachment att{
          .id = ColumnString(stmt.Get(), 0),
          .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(stmt.Get(), 1)),
          .file_path = ColumnString(stmt.Get(), 2),
          .mime_type = ColumnString(stmt.Get(), 3),
          .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(stmt.Get(), 4)),
          .thumbnail_base64 = ColumnString(stmt.Get(), 5),
          .hash = ColumnString(stmt.Get(), 6),
          .is_downloaded = (sqlite3_column_int(stmt.Get(), 7) != 0),
          .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(stmt.Get(), 8)),
          .note = ColumnString(stmt.Get(), 9),
          .file_name = ColumnString(stmt.Get(), 10)
      };
      uint64_t ts = static_cast<uint64_t>(sqlite3_column_int64(stmt.Get(), 11));
      results.push_back({std::move(att), ts});
    }
    return results;
  }

  auto GetAllAttachments(ContactID const& peer_id) -> std::vector<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::Attachment> attachments;
    SQLiteStatement const att_stmt(db_, R"SQL(
      SELECT id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
      FROM message_attachments
      WHERE peer_id = ?;
    )SQL");
    BindText(att_stmt.Get(), 1, peer_id.ToString());

    while (sqlite3_step(att_stmt.Get()) == SQLITE_ROW) {
      attachments.push_back(
          {.id = ColumnString(att_stmt.Get(), 0),
           .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(att_stmt.Get(), 1)),
           .file_path = ColumnString(att_stmt.Get(), 2),
           .mime_type = ColumnString(att_stmt.Get(), 3),
           .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(att_stmt.Get(), 4)),
           .thumbnail_base64 = ColumnString(att_stmt.Get(), 5),
           .hash = ColumnString(att_stmt.Get(), 6),
           .is_downloaded = (sqlite3_column_int(att_stmt.Get(), 7) != 0),
           .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(att_stmt.Get(), 8)),
           .note = ColumnString(att_stmt.Get(), 9),
           .file_name = ColumnString(att_stmt.Get(), 10)}
      );
    }
    return attachments;
  }

  auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    messaging::HistoryPage page;

    {
      SQLiteStatement const count_stmt(db_, "SELECT count(*) FROM messages WHERE peer_id = ?;");
      BindText(count_stmt.Get(), 1, peer_id.ToString());
      if (sqlite3_step(count_stmt.Get()) == SQLITE_ROW) {
        page.total_messages = sqlite3_column_int64(count_stmt.Get(), 0);
      }
    }

    SQLiteStatement const statement(db_, R"SQL(
      SELECT id, kind, timestamp, direction, status, is_truncated, content, is_edited, is_deleted, reply_to_id
      FROM messages WHERE peer_id = ? ORDER BY timestamp DESC LIMIT ? OFFSET ?;
    )SQL");
    BindText(statement.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(statement.Get(), 2, page_size);
    sqlite3_bind_int64(statement.Get(), 3, page_index * page_size);

    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = peer_id;

      if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&record.data)) {
        // v6 update: select `file_name`
        SQLiteStatement const att_stmt(db_, R"SQL(
          SELECT id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
          FROM message_attachments
          WHERE peer_id = ? AND message_id = ?;
        )SQL");
        BindText(att_stmt.Get(), 1, peer_id.ToString());
        sqlite3_bind_int64(att_stmt.Get(), 2, record.id);

        while (sqlite3_step(att_stmt.Get()) == SQLITE_ROW) {
          msg_entry->attachments.push_back(
              {.id = ColumnString(att_stmt.Get(), 0),
               .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(att_stmt.Get(), 1)),
               .file_path = ColumnString(att_stmt.Get(), 2),
               .mime_type = ColumnString(att_stmt.Get(), 3),
               .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(att_stmt.Get(), 4)),
               .thumbnail_base64 = ColumnString(att_stmt.Get(), 5),
               .hash = ColumnString(att_stmt.Get(), 6),
               .is_downloaded = (sqlite3_column_int(att_stmt.Get(), 7) != 0),
               .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(att_stmt.Get(), 8)),
               .note = ColumnString(att_stmt.Get(), 9),
               .file_name = ColumnString(att_stmt.Get(), 10)}
          );
        }
      }

      // Populate reactions
      SQLiteStatement const react_stmt(db_, R"SQL(
        SELECT author_id, reaction FROM reactions WHERE peer_id=? AND message_id=?;
      )SQL");
      BindText(react_stmt.Get(), 1, peer_id.ToString());
      sqlite3_bind_int64(react_stmt.Get(), 2, record.id);
      while (sqlite3_step(react_stmt.Get()) == SQLITE_ROW) {
        record.reactions.push_back(
            {ContactID(ColumnString(react_stmt.Get(), 0)),
             static_cast<messaging::Reaction>(sqlite3_column_int(react_stmt.Get(), 1))}
        );
      }

      page.messages.push_back(std::move(record));
    }
    std::ranges::reverse(page.messages);
    return page;
  }

  auto GetHistoryEntry(ContactID const& peer_id, uint64_t message_id)
      -> std::optional<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    SQLiteStatement const statement(db_, R"SQL(
      SELECT id, kind, timestamp, direction, status, is_truncated, content, is_edited, is_deleted, reply_to_id
      FROM messages WHERE peer_id = ? AND id = ?;
    )SQL");
    BindText(statement.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(statement.Get(), 2, message_id);

    if (sqlite3_step(statement.Get()) != SQLITE_ROW) {
      return std::nullopt;
    }

    auto record = ParseHistoryEntry(statement.Get());
    record.peer_id = peer_id;

    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&record.data)) {
      SQLiteStatement const att_stmt(db_, R"SQL(
        SELECT id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
        FROM message_attachments
        WHERE peer_id = ? AND message_id = ?;
      )SQL");
      BindText(att_stmt.Get(), 1, peer_id.ToString());
      sqlite3_bind_int64(att_stmt.Get(), 2, record.id);
      while (sqlite3_step(att_stmt.Get()) == SQLITE_ROW) {
        msg_entry->attachments.push_back(
            {.id = ColumnString(att_stmt.Get(), 0),
             .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(att_stmt.Get(), 1)),
             .file_path = ColumnString(att_stmt.Get(), 2),
             .mime_type = ColumnString(att_stmt.Get(), 3),
             .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(att_stmt.Get(), 4)),
             .thumbnail_base64 = ColumnString(att_stmt.Get(), 5),
             .hash = ColumnString(att_stmt.Get(), 6),
             .is_downloaded = (sqlite3_column_int(att_stmt.Get(), 7) != 0),
             .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(att_stmt.Get(), 8)),
             .note = ColumnString(att_stmt.Get(), 9),
             .file_name = ColumnString(att_stmt.Get(), 10)}
        );
      }
    }
    SQLiteStatement const react_stmt(
        db_,
        "SELECT author_id, reaction FROM reactions WHERE peer_id=? AND message_id=?;"
    );
    BindText(react_stmt.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(react_stmt.Get(), 2, record.id);
    while (sqlite3_step(react_stmt.Get()) == SQLITE_ROW) {
      record.reactions.push_back(
          {ContactID(ColumnString(react_stmt.Get(), 0)),
           static_cast<messaging::Reaction>(sqlite3_column_int(react_stmt.Get(), 1))}
      );
    }
    return record;
  }

  auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t const message_id)
      -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    SQLiteStatement const s(db_, "SELECT full_content FROM large_messages WHERE peer_id=? AND message_id=?;");
    BindText(s.Get(), 1, peer_id.ToString());
    sqlite3_bind_int64(s.Get(), 2, message_id);
    if (sqlite3_step(s.Get()) == SQLITE_ROW) return ColumnString(s.Get(), 0);
    return std::nullopt;
  }

  auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> override {
    if (peer_ids.empty()) return {};
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::ChatSummary> summaries;
    for (auto const& peer_id : peer_ids) {
      messaging::ChatSummary summary;
      summary.peer_id = peer_id;
      summary.unread_count = 0;
      {
        SQLiteStatement const s(
            db_,
            "SELECT COUNT(*) FROM messages WHERE peer_id=? AND direction=? AND status!=? AND kind=?;"
        );
        BindText(s.Get(), 1, peer_id.ToString());
        sqlite3_bind_int(s.Get(), 2, static_cast<int>(messaging::MessageDirection::received));
        sqlite3_bind_int(s.Get(), 3, static_cast<int>(messaging::MessageStatus::read));
        sqlite3_bind_int(s.Get(), 4, static_cast<int>(messaging::HistoryEntryKind::textMessage));
        if (sqlite3_step(s.Get()) == SQLITE_ROW) summary.unread_count = sqlite3_column_int(s.Get(), 0);
      }
      {
        SQLiteStatement const s(
            db_,
            "SELECT id, kind, timestamp, direction, status, is_truncated, content, is_edited, is_deleted, reply_to_id "
            "FROM messages WHERE "
            "peer_id=? ORDER BY timestamp DESC LIMIT 1;"
        );
        BindText(s.Get(), 1, peer_id.ToString());
        if (sqlite3_step(s.Get()) == SQLITE_ROW) {
          summary.last_message = ParseHistoryEntry(s.Get());
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

  auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus const status)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::HistoryEntryRecord> records;
    SQLiteStatement const statement(db_, R"SQL(
      SELECT id, kind, timestamp, direction, status, is_truncated, content, is_edited, is_deleted, reply_to_id
      FROM messages WHERE peer_id = ? AND status = ?;
    )SQL");
    BindText(statement.Get(), 1, peer_id.ToString());
    sqlite3_bind_int(statement.Get(), 2, static_cast<int>(status));

    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = peer_id;

      if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&record.data)) {
        // v6 update: select `file_name`
        SQLiteStatement const att_stmt(db_, R"SQL(
          SELECT id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
          FROM message_attachments
          WHERE peer_id = ? AND message_id = ?;
        )SQL");
        BindText(att_stmt.Get(), 1, peer_id.ToString());
        sqlite3_bind_int64(att_stmt.Get(), 2, record.id);

        while (sqlite3_step(att_stmt.Get()) == SQLITE_ROW) {
          msg_entry->attachments.push_back(
              {.id = ColumnString(att_stmt.Get(), 0),
               .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(att_stmt.Get(), 1)),
               .file_path = ColumnString(att_stmt.Get(), 2),
               .mime_type = ColumnString(att_stmt.Get(), 3),
               .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(att_stmt.Get(), 4)),
               .thumbnail_base64 = ColumnString(att_stmt.Get(), 5),
               .hash = ColumnString(att_stmt.Get(), 6),
               .is_downloaded = (sqlite3_column_int(att_stmt.Get(), 7) != 0),
               .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(att_stmt.Get(), 8)),
               .note = ColumnString(att_stmt.Get(), 9),
               .file_name = ColumnString(att_stmt.Get(), 10)}
          );
        }
      }
      records.push_back(std::move(record));
    }
    return records;
  }

  auto GetPendingAttachments(ContactID const& peer_id) -> std::vector<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);
    std::vector<messaging::Attachment> attachments;
    // v6 update: select `file_name`
    SQLiteStatement const att_stmt(db_, R"SQL(
      SELECT id, type, file_path, mime_type, size_bytes, thumbnail_base64, hash, is_downloaded, status, note, file_name
      FROM message_attachments
      WHERE peer_id = ? AND (status = 0 OR status = 1) AND is_downloaded = 0;
    )SQL");
    BindText(att_stmt.Get(), 1, peer_id.ToString());

    while (sqlite3_step(att_stmt.Get()) == SQLITE_ROW) {
      attachments.push_back(
          {.id = ColumnString(att_stmt.Get(), 0),
           .type = static_cast<messaging::AttachmentType>(sqlite3_column_int(att_stmt.Get(), 1)),
           .file_path = ColumnString(att_stmt.Get(), 2),
           .mime_type = ColumnString(att_stmt.Get(), 3),
           .size_bytes = static_cast<uint64_t>(sqlite3_column_int64(att_stmt.Get(), 4)),
           .thumbnail_base64 = ColumnString(att_stmt.Get(), 5),
           .hash = ColumnString(att_stmt.Get(), 6),
           .is_downloaded = (sqlite3_column_int(att_stmt.Get(), 7) != 0),
           .status = static_cast<messaging::AttachmentStatus>(sqlite3_column_int(att_stmt.Get(), 8)),
           .note = ColumnString(att_stmt.Get(), 9),
           .file_name = ColumnString(att_stmt.Get(), 10)}
      );
    }
    return attachments;
  }

  // --- Search ---
  auto SearchMessages(ContactID const& peer_id, std::string const& query)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    std::string const pattern = "%" + query + "%";

    // Search both standard content and large messages (full_content).
    // Restrict to text and system messages.
    SQLiteStatement const statement(
        db_,
        R"SQL(
      SELECT m.id, m.kind, m.timestamp, m.direction, m.status, m.is_truncated,
             CASE WHEN m.is_truncated = 1 THEN lm.full_content ELSE m.content END,
             m.is_edited, m.is_deleted, m.reply_to_id
      FROM messages m
      LEFT JOIN large_messages lm ON m.peer_id = lm.peer_id AND m.id = lm.message_id
      WHERE m.peer_id = ?
        AND (m.kind = 0 OR m.kind = 3)
        AND (
          (m.is_truncated = 0 AND lower(m.content) LIKE lower(?))
          OR
          (m.is_truncated = 1 AND lower(lm.full_content) LIKE lower(?))
        )
      ORDER BY m.timestamp DESC;
    )SQL"
    );

    BindText(statement.Get(), 1, peer_id.ToString());
    BindText(statement.Get(), 2, pattern);
    BindText(statement.Get(), 3, pattern);

    std::vector<messaging::HistoryEntryRecord> results;
    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = peer_id;
      results.push_back(std::move(record));
    }
    return results;
  }

  auto SearchAllMessages(std::string const& query) -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    INVARIANT(db_);

    std::string const pattern = "%" + query + "%";

    SQLiteStatement const statement(
        db_,
        R"SQL(
      SELECT m.id, m.kind, m.timestamp, m.direction, m.status, m.is_truncated,
             CASE WHEN m.is_truncated = 1 THEN lm.full_content ELSE m.content END,
             m.is_edited, m.is_deleted, m.reply_to_id, m.peer_id
      FROM messages m
      LEFT JOIN large_messages lm ON m.peer_id = lm.peer_id AND m.id = lm.message_id
      WHERE (m.kind = 0 OR m.kind = 3)
        AND (
          (m.is_truncated = 0 AND lower(m.content) LIKE lower(?))
          OR
          (m.is_truncated = 1 AND lower(lm.full_content) LIKE lower(?))
        )
      ORDER BY m.timestamp DESC;
    )SQL"
    );

    BindText(statement.Get(), 1, pattern);
    BindText(statement.Get(), 2, pattern);

    std::vector<messaging::HistoryEntryRecord> results;
    while (sqlite3_step(statement.Get()) == SQLITE_ROW) {
      auto record = ParseHistoryEntry(statement.Get());
      record.peer_id = ContactID(ColumnString(statement.Get(), 10));
      results.push_back(std::move(record));
    }
    return results;
  }

 private:
  void RunMigrations() const {
    int user_version = 0;
    {
      SQLiteStatement const get_version_stmt(db_, "PRAGMA user_version;");
      if (sqlite3_step(get_version_stmt.Get()) == SQLITE_ROW) {
        user_version = sqlite3_column_int(get_version_stmt.Get(), 0);
      }
    }

    // NOLINTBEGIN(*-braces-around-statements)
    if (user_version < 1) migrations::MigrateToV0001(db_);
    if (user_version < 2) migrations::MigrateToV0002(db_);
    if (user_version < 3) migrations::MigrateToV0003(db_);
    if (user_version < 4) migrations::MigrateToV0004(db_);
    if (user_version < 5) migrations::MigrateToV0005(db_);
    if (user_version < 6) migrations::MigrateToV0006(db_);
    if (user_version < 7) migrations::MigrateToV0007(db_);
    if (user_version < 8) migrations::MigrateToV0008(db_);
    if (user_version < 9) migrations::MigrateToV0009(db_);
    if (user_version < 10) migrations::MigrateToV0010(db_);
    if (user_version < 11) migrations::MigrateToV0011(db_);
    if (user_version < 12) migrations::MigrateToV0012(db_);
    // NOLINTEND(*-braces-around-statements)
  }

  static auto ParseHistoryEntry(sqlite3_stmt* stmt) -> messaging::HistoryEntryRecord {
    messaging::HistoryEntryRecord record{
        .id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0)),
        .kind = static_cast<messaging::HistoryEntryKind>(sqlite3_column_int(stmt, 1)),
        .timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2)),
        .direction = static_cast<messaging::MessageDirection>(sqlite3_column_int(stmt, 3)),
        .status = static_cast<messaging::MessageStatus>(sqlite3_column_int(stmt, 4)),
        .is_truncated = (sqlite3_column_int(stmt, 5) == 1),
        .is_edited = (sqlite3_column_int(stmt, 7) == 1),
        .is_deleted = (sqlite3_column_int(stmt, 8) == 1),
    };

    switch (record.kind) {
      case messaging::HistoryEntryKind::textMessage:
      case messaging::HistoryEntryKind::systemMessage:
        record.data = messaging::MessageEntry{
            .content = ColumnString(stmt, 6),
            .attachments = {},
            .reply_to_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9))
        };
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
