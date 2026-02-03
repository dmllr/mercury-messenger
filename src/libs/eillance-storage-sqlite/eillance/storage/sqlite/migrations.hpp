#pragma once

// IMPORTANT NOTE: All migrations MUST be self-contained transactions.
// If a migration fails, the database must remain unchanged (atomic operation).
// Each migration routine is responsible for executing "BEGIN TRANSACTION" at the start
// and "COMMIT" only upon successful completion of all schema changes and data updates.
// Explicitly increment the user_version PRAGMA within the same transaction.

#include "eillance/storage/sqlite/migrations/0001_initial_schema.hpp"
#include "eillance/storage/sqlite/migrations/0002_add_attachments.hpp"
#include "eillance/storage/sqlite/migrations/0003_resilience.hpp"
#include "eillance/storage/sqlite/migrations/0004_attachment_status.hpp"
#include "eillance/storage/sqlite/migrations/0005_add_attachment_note.hpp"
#include "eillance/storage/sqlite/migrations/0006_add_filename.hpp"
