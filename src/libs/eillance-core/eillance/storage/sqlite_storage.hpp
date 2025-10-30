#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite_orm/sqlite_orm.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::storage {

namespace db {
// Database model for a key-value setting.
struct Setting {
  std::string key;
  std::string value;
};

// Database model for a contact.
struct Contact {
  std::string peer_id;
  int status;  // Corresponds to identity::ContactStatus
  std::optional<std::string> display_name;
  std::optional<std::string> display_name_override;
  std::optional<std::string> bio;
  std::optional<std::string> photo_base64;
};

// Database model for a history entry (message or call).
struct HistoryEntry {
  int64_t id;
  std::string peer_id;
  int kind;          // Corresponds to messaging::HistoryEntryKind
  uint64_t timestamp;  // Corresponds to Time::Unit
  int direction;     // Corresponds to messaging::MessageDirection
  int status;        // Corresponds to messaging::MessageStatus
  std::string content;
  int duration_seconds = 0;
  int call_direction = 0;
  int call_status = 0;
};

// Helper function to create the database schema using sqlite_orm.
inline auto CreateStorage(const std::string& path) {
  using namespace sqlite_orm;
  return make_storage(path,
                      make_table("settings", make_column("key", &Setting::key, primary_key()), make_column("value", &Setting::value)),
                      make_table("contacts",
                                 make_column("peer_id", &Contact::peer_id, primary_key()),
                                 make_column("status", &Contact::status),
                                 make_column("display_name", &Contact::display_name),
                                 make_column("display_name_override", &Contact::display_name_override),
                                 make_column("bio", &Contact::bio),
                                 make_column("photo_base64", &Contact::photo_base64)),
                      make_table("history", make_column("id", &HistoryEntry::id, primary_key().autoincrement()), make_column("peer_id", &HistoryEntry::peer_id), make_column("kind", &HistoryEntry::kind), make_column("timestamp", &HistoryEntry::timestamp), make_column("direction", &HistoryEntry::direction), make_column("status", &HistoryEntry::status), make_column("content", &HistoryEntry::content), make_column("duration_seconds", &HistoryEntry::duration_seconds), make_column("call_direction", &HistoryEntry::call_direction), make_column("call_status", &HistoryEntry::call_status)),
                      make_index("idx_history_peer_timestamp", &HistoryEntry::peer_id, &HistoryEntry::timestamp));
}

// Type alias for the storage object.
using Storage = decltype(CreateStorage(""));

// --- Conversion functions between domain models and database models ---

inline auto ToDomainContact(const db::Contact& db_contact) -> identity::Contact {
  return {
      .peer_id = db_contact.peer_id,
      .status = static_cast<identity::ContactStatus>(db_contact.status),
      .display_name = db_contact.display_name,
      .display_name_override = db_contact.display_name_override,
      .bio = db_contact.bio,
      .photo_base64 = db_contact.photo_base64};
}

inline auto FromDomainContact(const identity::Contact& contact) -> db::Contact {
  return {
      .peer_id = contact.peer_id,
      .status = static_cast<int>(contact.status),
      .display_name = contact.display_name,
      .display_name_override = contact.display_name_override,
      .bio = contact.bio,
      .photo_base64 = contact.photo_base64};
}

inline auto ToDomainHistoryEntry(const db::HistoryEntry& db_entry) -> messaging::HistoryEntryRecord {
  messaging::HistoryEntryRecord record{
      .id = static_cast<uint64_t>(db_entry.id),
      .kind = static_cast<messaging::HistoryEntryKind>(db_entry.kind),
      .timestamp = db_entry.timestamp,
      .direction = static_cast<messaging::MessageDirection>(db_entry.direction),
      .status = static_cast<messaging::MessageStatus>(db_entry.status),
  };

  switch (record.kind) {
    case messaging::HistoryEntryKind::textMessage:
    case messaging::HistoryEntryKind::systemMessage:
      record.data = messaging::MessageEntry{db_entry.content};
      break;
    case messaging::HistoryEntryKind::voiceCall:
    case messaging::HistoryEntryKind::videoCall:
      record.data = messaging::CallEntry{.duration_seconds = static_cast<uint32_t>(db_entry.duration_seconds), .call_direction = static_cast<messaging::CallDirection>(db_entry.call_direction), .call_status = static_cast<messaging::CallStatus>(db_entry.call_status)};
      break;
  }
  return record;
}

inline auto FromDomainHistoryEntry(const messaging::HistoryEntryRecord& record, const std::string& peer_id) -> db::HistoryEntry {
  db::HistoryEntry db_entry{
      .id = static_cast<int64_t>(record.id),
      .peer_id = peer_id,
      .kind = static_cast<int>(record.kind),
      .timestamp = record.timestamp,
      .direction = static_cast<int>(record.direction),
      .status = static_cast<int>(record.status),
  };

  std::visit(
      [&db_entry](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
          db_entry.content = arg.content;
        } else if constexpr (std::is_same_v<T, messaging::CallEntry>) {
          db_entry.duration_seconds = arg.duration_seconds;
          db_entry.call_direction = static_cast<int>(arg.call_direction);
          db_entry.call_status = static_cast<int>(arg.call_status);
        }
      },
      record.data);

  return db_entry;
}

}  // namespace db

class SQLiteStorage final : public PersistentStorageImpl {
  // Use a recursive_mutex to allow re-entrant calls from the same thread,
  // which can happen with complex logic flows.
  std::recursive_mutex mutex_;
  std::unique_ptr<db::Storage> storage_;

public:
  explicit SQLiteStorage(const std::filesystem::path& db_path) {
    try {
      storage_ = std::make_unique<db::Storage>(db::CreateStorage(db_path.string()));
      storage_->sync_schema();
    } catch (const std::exception& e) {
      LOGE << "Failed to initialize SQLite database at " << db_path.string() << ": " << e.what();
      throw;
    }
  }

  // --- Settings ---
  auto GetSetting(std::string_view key) -> std::optional<std::string> override {
    const std::lock_guard lock(mutex_);
    if (auto setting = storage_->get_pointer<db::Setting>(std::string(key))) { return setting->value; }
    return std::nullopt;
  }
  void SetSetting(std::string_view key, std::string_view value) override {
    const std::lock_guard lock(mutex_);
    storage_->replace(db::Setting{std::string(key), std::string(value)});
  }
  void DeleteSetting(std::string_view key) override {
    const std::lock_guard lock(mutex_);
    storage_->remove<db::Setting>(std::string(key));
  }

  // --- Raw Key-Value for backward compatibility if needed ---
  auto GetRaw(const std::string& key) -> std::optional<std::vector<std::byte>> override {
    if (auto val = GetSetting(key)) { return crypto::FromHex(*val); }
    return std::nullopt;
  }

  auto SetRaw(const std::string& key, const std::vector<std::byte>& value) -> bool override {
    SetSetting(key, crypto::ToHex(value));
    return true;
  }

  // --- Contacts ---
  auto GetContacts() -> std::vector<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    auto db_contacts = storage_->get_all<db::Contact>();
    std::vector<identity::Contact> contacts;
    contacts.reserve(db_contacts.size());
    for (const auto& db_contact : db_contacts) {
      contacts.push_back(db::ToDomainContact(db_contact));
    }
    return contacts;
  }

  auto GetContact(std::string_view peer_id) -> std::optional<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    if (auto db_contact = storage_->get_pointer<db::Contact>(std::string(peer_id))) { return db::ToDomainContact(*db_contact); }
    return std::nullopt;
  }

  void UpdateContact(const identity::Contact& contact) override {
    const std::lock_guard lock(mutex_);
    storage_->replace(db::FromDomainContact(contact));
  }

  void RemoveContact(std::string_view peer_id) override {
    const std::lock_guard lock(mutex_);
    storage_->remove<db::Contact>(std::string(peer_id));
  }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    const std::lock_guard lock(mutex_);
    auto db_entry = db::FromDomainHistoryEntry(entry, entry.peer_id_for_db);
    db_entry.id = storage_->insert(db_entry);
    entry.id = static_cast<uint64_t>(db_entry.id);
    return entry;
  }

  void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) override {
    using namespace sqlite_orm;
    const std::lock_guard lock(mutex_);
    storage_->update_all(set(c(&db::HistoryEntry::status) = static_cast<int>(new_status)), where(c(&db::HistoryEntry::id) == static_cast<int64_t>(message_id) and c(&db::HistoryEntry::peer_id) == peer_id));
  }

  void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) override {
    using namespace sqlite_orm;
    const std::lock_guard lock(mutex_);
    std::vector<int64_t> ids_64;
    ids_64.reserve(message_ids.size());
    for (const auto id : message_ids) {
      ids_64.push_back(static_cast<int64_t>(id));
    }
    storage_->update_all(set(c(&db::HistoryEntry::status) = static_cast<int>(new_status)), where(c(&db::HistoryEntry::peer_id) == peer_id and in(&db::HistoryEntry::id, ids_64)));
  }

  auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) -> messaging::HistoryPage override {
    using namespace sqlite_orm;
    const std::lock_guard lock(mutex_);
    messaging::HistoryPage page;
    page.total_messages = storage_->count<db::HistoryEntry>(where(c(&db::HistoryEntry::peer_id) == peer_id));

    auto db_entries = storage_->get_all<db::HistoryEntry>(where(c(&db::HistoryEntry::peer_id) == peer_id), order_by(&db::HistoryEntry::timestamp).desc(), limit(page_size, offset(page_index * page_size)));

    page.messages.reserve(db_entries.size());
    for (const auto& db_entry : db_entries) {
      page.messages.push_back(db::ToDomainHistoryEntry(db_entry));
    }
    std::ranges::reverse(page.messages);  // Reverse to get ascending time order for display
    return page;
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<messaging::ChatSummary> override {
    using namespace sqlite_orm;
    const std::lock_guard lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (const auto& peer_id : peer_ids) {
      auto last_entry = storage_->get_all<db::HistoryEntry>(where(c(&db::HistoryEntry::peer_id) == peer_id), order_by(&db::HistoryEntry::timestamp).desc(), limit(1));

      if (!last_entry.empty()) {
        auto unread_count = storage_->count<db::HistoryEntry>(where(c(&db::HistoryEntry::peer_id) == peer_id and c(&db::HistoryEntry::direction) == static_cast<int>(messaging::MessageDirection::received) and c(&db::HistoryEntry::status) != static_cast<int>(messaging::MessageStatus::read) and c(&db::HistoryEntry::kind) == static_cast<int>(messaging::HistoryEntryKind::textMessage)));
        summaries.push_back({.peer_id = peer_id, .last_message = db::ToDomainHistoryEntry(last_entry[0]), .unread_count = static_cast<uint32_t>(unread_count)});
      }
    }

    std::ranges::sort(summaries, [](const auto& a, const auto& b) { return a.last_message.timestamp > b.last_message.timestamp; });
    return summaries;
  }

  auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) -> std::vector<messaging::HistoryEntryRecord> override {
    using namespace sqlite_orm;
    const std::lock_guard lock(mutex_);
    auto db_entries = storage_->get_all<db::HistoryEntry>(where(c(&db::HistoryEntry::peer_id) == peer_id and c(&db::HistoryEntry::status) == static_cast<int>(status)));
    std::vector<messaging::HistoryEntryRecord> records;
    records.reserve(db_entries.size());
    for (const auto& entry : db_entries) {
      records.push_back(db::ToDomainHistoryEntry(entry));
    }
    return records;
  }
};

}  // namespace eillance::storage
