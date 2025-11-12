#pragma once

#include <sqlite3.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/sqlite_helpers.hpp"

namespace eillance::storage::migrations {

inline void migrate_to_v4(sqlite3* db) {
#ifndef NDEBUG
  LOGI << "Populating database with fake conversations for screenshots (DEBUG build only).";

  char* err_msg = nullptr;
  if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to begin V4 migration transaction: ";
    if (err_msg) {
      error += err_msg;
      sqlite3_free(err_msg);
    }
    throw std::runtime_error(error);
  }

  try {
    // --- Define Contact Info ---
    const std::string sarah_id = "b1a2318c6ed35b5420372332158321a56611f0c2a559869a19c636139c287a6f";
    const std::string chloe_id = "c3b4429d7fe46c6531483443269432b6772201d3b66a97a02ad74724ad398b70";
    const std::string kenji_id = "d4c553ae80f57d764259455437a543c7883302e4c77ba8b13be85835be4a9c81";
    // Placeholder 1x1 pixel base64 images
    const std::string sarah_photo_b64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=";
    const std::string chloe_photo_b64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9mQAAAABJRU5ErkJggg==";
    const std::string kenji_photo_b64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkWBluegAECgG9V21sewAAAABJRU5ErkJggg==";

    // --- Insert Contacts ---
    const SQLiteStatement contact_stmt(db, R"SQL(
        INSERT INTO contacts (peer_id, status, display_name, bio, photo_base64) VALUES (?, ?, ?, ?, ?);
    )SQL");

    // Sarah
    BindText(contact_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(contact_stmt.Get(), 2, static_cast<int>(identity::ContactStatus::confirmed));
    BindText(contact_stmt.Get(), 3, "Sarah");
    BindText(contact_stmt.Get(), 4, "Lover of hiking and good coffee ☕️");
    BindText(contact_stmt.Get(), 5, sarah_photo_b64);
    if (sqlite3_step(contact_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert Sarah"); }

    // Chloe
    sqlite3_reset(contact_stmt.Get());
    BindText(contact_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(contact_stmt.Get(), 2, static_cast<int>(identity::ContactStatus::confirmed));
    BindText(contact_stmt.Get(), 3, "Chloe");
    BindText(contact_stmt.Get(), 4, "Traveler. Designer. Dreamer.");
    BindText(contact_stmt.Get(), 5, chloe_photo_b64);
    if (sqlite3_step(contact_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert Chloe"); }

    // Kenji
    sqlite3_reset(contact_stmt.Get());
    BindText(contact_stmt.Get(), 1, kenji_id);
    sqlite3_bind_int(contact_stmt.Get(), 2, static_cast<int>(identity::ContactStatus::confirmed));
    BindText(contact_stmt.Get(), 3, "健司 (Kenji)");
    BindText(contact_stmt.Get(), 4, "東京 | Tech & Ramen");
    BindText(contact_stmt.Get(), 5, kenji_photo_b64);
    if (sqlite3_step(contact_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert Kenji"); }

    // --- Timestamps ---
    using namespace std::chrono;
    const auto now = system_clock::now();
    auto us_now = duration_cast<microseconds>(now.time_since_epoch());

    // --- Insert History ---
    const SQLiteStatement msg_stmt(db, R"SQL(
        INSERT INTO history (peer_id, kind, timestamp, direction, status, content) VALUES (?, ?, ?, ?, ?, ?);
    )SQL");
    const SQLiteStatement call_stmt(db, R"SQL(
        INSERT INTO history (peer_id, kind, timestamp, direction, status, duration_seconds, call_direction, call_status) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL");

    // Conversation 1: Alex(Me) & Sarah
    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - minutes(5)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::delivered));
    BindText(msg_stmt.Get(), 6, "Woah, this app is cool. I didn't have to give my phone number or email!");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo1-msg1"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - minutes(4)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "Right? It just gives you an anonymous ID. Super private.");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo1-msg2"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - minutes(4) + seconds(5)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::delivered));
    BindText(msg_stmt.Get(), 6, "So nobody can link my chats to my real identity? That's awesome!");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo1-msg3"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - minutes(3)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "Exactly. And it's all peer-to-peer encrypted. No servers spying on us. 😎");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo1-msg4"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, sarah_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - minutes(2)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::sent));
    BindText(msg_stmt.Get(), 6, "Finally! A messenger that actually respects privacy.");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo1-msg5"); }

    // Conversation 2: Ben(Me) & Chloe
    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - days(2)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "Just booked the flights for our Kyoto trip! ✈️ Can't wait!");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo2-msg1"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - days(2) + minutes(1)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "OMG yes! So excited! Let's do a quick video call tonight to plan the itinerary?");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo2-msg2"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - days(2) + minutes(2)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "Perfect. The connection is always so clear on here. And since it's P2P encrypted, we can talk about the details without worrying.");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo2-msg3"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - days(2) + minutes(3)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "Totally. It feels way more secure than other apps. See you at 8!");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo2-msg4"); }

    sqlite3_reset(call_stmt.Get());
    BindText(call_stmt.Get(), 1, chloe_id);
    sqlite3_bind_int(call_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::videoCall));
    sqlite3_bind_int64(call_stmt.Get(), 3, (us_now - days(1)).count());
    sqlite3_bind_int(call_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(call_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::none));
    sqlite3_bind_int(call_stmt.Get(), 6, 1823);
    sqlite3_bind_int(call_stmt.Get(), 7, static_cast<int>(messaging::CallDirection::outgoing));
    sqlite3_bind_int(call_stmt.Get(), 8, static_cast<int>(messaging::CallStatus::answered));
    if (sqlite3_step(call_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo2-call1"); }

    // Conversation 3: Yumi(Me) & Kenji
    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, kenji_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - hours(1)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "新しいスマホにしたよ！やっと設定が終わった…");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo3-msg1"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, kenji_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - hours(1) + minutes(1)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "おめでとう！MercuryのIDはちゃんと復元できた？回復フレーズ使った？");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo3-msg2"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, kenji_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - hours(1) + minutes(2)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::sent));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "うん、もちろん！12個の単語を入れるだけで、連絡先も全部元通り。すごく簡単だった。");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo3-msg3"); }

    sqlite3_reset(msg_stmt.Get());
    BindText(msg_stmt.Get(), 1, kenji_id);
    sqlite3_bind_int(msg_stmt.Get(), 2, static_cast<int>(messaging::HistoryEntryKind::textMessage));
    sqlite3_bind_int64(msg_stmt.Get(), 3, (us_now - hours(1) + minutes(3)).count());
    sqlite3_bind_int(msg_stmt.Get(), 4, static_cast<int>(messaging::MessageDirection::received));
    sqlite3_bind_int(msg_stmt.Get(), 5, static_cast<int>(messaging::MessageStatus::read));
    BindText(msg_stmt.Get(), 6, "だよね。登録不要で、これだけ安全なのはすごい。さすがP2P暗号化！(笑)");
    if (sqlite3_step(msg_stmt.Get()) != SQLITE_DONE) { throw std::runtime_error("Failed to insert convo3-msg4"); }

  } catch (const std::exception& e) {
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    LOGE << "Migration to V4 failed: " << e.what();
    throw;
  }

  if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
    std::string error = "Failed to commit V4 migration transaction: ";
    if (err_msg) {
      error += err_msg;
      sqlite3_free(err_msg);
    }
    throw std::runtime_error(error);
  }
  LOGI << "Migration to V4 successful.";

#else
  (void)db;  // Avoid unused parameter warning in release builds
  LOGI << "Skipping fake data population in RELEASE build.";
#endif
}

}  // namespace eillance::storage::migrations
