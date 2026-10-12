#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "eillance/logging/logging.hpp"

namespace eillance::storage {

class SQLiteStatement {
  sqlite3_stmt* stmt_ = nullptr;

 public:
  explicit SQLiteStatement(sqlite3* db, std::string const& query) {
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
      LGE << "SQLite prepare failed: " << sqlite3_errmsg(db);
      throw std::runtime_error("SQLite prepare failed");
    }
  }

  ~SQLiteStatement() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  SQLiteStatement(SQLiteStatement const&) = delete;

  auto operator=(SQLiteStatement const&) -> SQLiteStatement& = delete;

  SQLiteStatement(SQLiteStatement&& other) noexcept
      : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
  }

  auto operator=(SQLiteStatement&& other) noexcept -> SQLiteStatement& {
    if (this != &other) {
      if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
      }
      stmt_ = other.stmt_;
      other.stmt_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] auto Get() const -> sqlite3_stmt* {
    return stmt_;
  }
};

inline void BindText(sqlite3_stmt* stmt, int const idx, std::string const& val) {
  sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
}

inline void BindOptionalText(sqlite3_stmt* stmt, int const idx, std::optional<std::string> const& val) {
  if (val) {
    sqlite3_bind_text(stmt, idx, val->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, idx);
  }
}

inline auto ColumnString(sqlite3_stmt* stmt, int const idx) -> std::string {
  auto const* text = reinterpret_cast<char const*>(sqlite3_column_text(stmt, idx));
  return (text != nullptr) ? std::string(text) : "";
}

inline auto ColumnOptionalString(sqlite3_stmt* stmt, int const idx) -> std::optional<std::string> {
  if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  return ColumnString(stmt, idx);
}

}  // namespace eillance::storage
