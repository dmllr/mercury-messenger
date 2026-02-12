#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "eillance/capture/audio_capture.hpp"
#include "eillance/capture/video_capture.hpp"
#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/utils/args_parser.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/view_model/call_view_model.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "eillance/view_model/contacts_view_model.hpp"
#include "eillance/view_model/conversation_view_model.hpp"
#include "eillance/view_model/profile_view_model.hpp"
#include "eillance/view_model/settings_view_model.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

namespace {

// Helper function to format contact entries for the ftxui::Menu component.
std::vector<std::string> GetContactMenuEntries(
    std::vector<eillance::view_model::ViewContact> const& contacts,
    eillance::view_model::CallState const& call_state
) {
  std::vector<std::string> entries;
  for (auto const& contact : contacts) {
    if (contact.status == eillance::identity::ContactStatus::rejected) {
      continue;
    }

    std::string status_str;
    switch (contact.status) {
      case eillance::identity::ContactStatus::pendingApproval:
        status_str = " [pending]";
        break;
      case eillance::identity::ContactStatus::awaitingApproval:
        status_str = " [ACTION REQUIRED]";
        break;
      case eillance::identity::ContactStatus::confirmed:
        status_str = "";
        break;
      case eillance::identity::ContactStatus::rejected:
        status_str = " [blocked]";
        break;
    }

    std::string conn_status_str;
    if (call_state.peer_id == contact.peer_id &&
        (call_state.is_connecting || call_state.is_ringing || call_state.is_in_call)) {
      if (call_state.is_connecting) {
        conn_status_str = " (Calling)";
      } else if (call_state.is_ringing) {
        conn_status_str = " (Ringing)";
      } else if (call_state.is_in_call) {
        conn_status_str = " (In Call)";
      }
    } else {
      switch (contact.connection_state) {
        case eillance::transport::PeerConnectionState::connecting:
          conn_status_str = " (Connecting)";
          break;
        case eillance::transport::PeerConnectionState::connected:
          conn_status_str = " (Connected)";
          break;
        case eillance::transport::PeerConnectionState::failed:
          conn_status_str = " (Failed)";
          break;
        default:
          break;
      }
    }

    std::string unread_count_str;
    if (contact.unread_count > 0) {
      unread_count_str = " [" + std::to_string(contact.unread_count) + "]";
    }

    entries.push_back(contact.display_name + status_str + conn_status_str + unread_count_str);
  }
  return entries;
}

constexpr auto GetConnectionStatusString(eillance::ServiceConnectionState const state) -> std::string_view {
  switch (state) {
    case eillance::ServiceConnectionState::establishing:
      return "CONNECTING";
    case eillance::ServiceConnectionState::online:
      return "ONLINE";
    case eillance::ServiceConnectionState::failure:
      return "CONNECTION FAILED";
    case eillance::ServiceConnectionState::disconnected:
      return "OFFLINE";
    default:
      return "UNKNOWN";
  }
}

// Helper function to format timestamps according to the specified rules.
auto FormatTimestamp(Time::Unit const ts_microseconds) -> std::string {
  using namespace std::chrono;
  time_t const message_time_t = ts_microseconds / 1000000;
  auto const now_tp = system_clock::now();
  time_t const now_time_t = system_clock::to_time_t(now_tp);

#ifdef _WIN32
  tm message_tm{};
  tm now_tm{};
  localtime_s(&message_tm, &message_time_t);
  localtime_s(&now_tm, &now_time_t);
#else
  tm message_tm{};
  tm now_tm{};
  localtime_r(&message_time_t, &message_tm);
  localtime_r(&now_time_t, &now_tm);
#endif

  constexpr auto k_buffer_size = 64;
  char buffer[k_buffer_size];

  if (message_tm.tm_year == now_tm.tm_year && message_tm.tm_yday == now_tm.tm_yday) {
    strftime(buffer, sizeof(buffer), "%H:%M", &message_tm);
    return std::string(buffer);
  }

  constexpr auto k_seconds_in_day = 86400;
  time_t const yesterday_time_t = now_time_t - k_seconds_in_day;
#ifdef _WIN32
  tm yesterday_tm{};
  localtime_s(&yesterday_tm, &yesterday_time_t);
#else
  tm yesterday_tm{};
  localtime_r(&yesterday_time_t, &yesterday_tm);
#endif

  if (message_tm.tm_year == yesterday_tm.tm_year && message_tm.tm_yday == yesterday_tm.tm_yday) {
    strftime(buffer, sizeof(buffer), "Yesterday, %H:%M", &message_tm);
    return std::string(buffer);
  }

  if (message_tm.tm_year == now_tm.tm_year) {
    strftime(buffer, sizeof(buffer), "%b %d, %H:%M", &message_tm);
    return std::string(buffer);
  }

  strftime(buffer, sizeof(buffer), "%Y, %b %d, %H:%M", &message_tm);
  return std::string(buffer);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  eillance::utils::ArgsParser const args(argc, argv);
  std::string const config_name_str = args.GetPositional(0).value_or("Default");
  char const* config_name = config_name_str.c_str();

#ifdef _WIN32
  char const* home_dir_env = getenv("USERPROFILE");
#else
  char const* home_dir_env = getenv("HOME");
#endif
  if (home_dir_env == nullptr) {
    std::cerr << "Could not determine home directory.\n";
    return 1;
  }
  auto config_path = std::filesystem::path(home_dir_env) / ".mercury" / config_name;

  // --- Logging Initialization ---
  auto log_file_path = config_path / "mercury-cli.log";
  std::filesystem::create_directories(log_file_path.parent_path());
  auto const u8_path = log_file_path.u8string();
  auto const log_file_path_str{std::string{reinterpret_cast<char const*>(u8_path.c_str()), u8_path.length()}};
  constexpr auto max_log_size_bytes = 5 * 1024 * 1024;
  constexpr auto max_log_files = 3;
  static plog::RollingFileAppender<plog::TxtFormatter> file_appender(
      log_file_path_str.c_str(),
      max_log_size_bytes,
      max_log_files
  );
  Log::Initialize(&file_appender, plog::info);

  // --- Service Layer (Model) Initialization ---
  auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(config_path / "mercury.db");
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));

  auto rtc_transport_impl = std::make_unique<eillance::rtc::RtcTransport>();
  auto transport = std::make_shared<eillance::transport::Transport>(std::move(rtc_transport_impl));
  auto client = std::make_shared<eillance::Client>(storage, transport);

  client->SetVideoCapture(std::make_unique<eillance::VideoCaptureCli>());
  client->SetAudioCapture(std::make_unique<eillance::AudioCaptureCli>());

  auto router = std::make_shared<eillance::view_model::ClientEventRouter>(client);

  // CLI Argument Handling (Backup/Restore)
  if (auto backup_path = args.Get("--backup")) {
    std::cout << "Enter password for backup: ";
    std::string password;
    std::getline(std::cin, password);
    if (client->CreateBackup(*backup_path, password)) {
      std::cout << "Backup created successfully at " << *backup_path << "\n";
    } else {
      std::cerr << "Backup failed.\n";
    }
    return 0;
  }

  if (auto restore_path = args.Get("--restore")) {
    std::cout << "Enter password for backup file: ";
    std::string password;
    std::getline(std::cin, password);
    if (client->RestoreBackup(*restore_path, password)) {
      std::cout << "Restore successful. Please restart the application.\n";
    } else {
      std::cerr << "Restore failed. Check password or file integrity.\n";
    }
    return 0;
  }

  // --- Identity Setup ---
  if (!client->IdentityExists()) {
    std::cout << "Welcome to Mercury! No identity found.\n"
              << "1. Create a new identity\n"
              << "2. Restore from a 12-word recovery phrase\n"
              << "Enter your choice: ";
    int choice = 0;
    std::cin >> choice;
    std::cin.ignore();
    if (choice == 1) {
      auto mnemonic = client->GenerateNewIdentity();
      std::cout << "\nIMPORTANT: Save this recovery phrase securely:\n\n"
                << mnemonic << "\n\nPress Enter to continue...";
      std::cin.get();
    } else if (choice == 2) {
      std::cout << "Enter your 12-word recovery phrase: ";
      std::string phrase;
      std::getline(std::cin, phrase);
      if (!client->RestoreIdentity(phrase)) {
        std::cerr << "Invalid phrase.\n";
        return 1;
      }
    } else {
      return 1;
    }
  }

  if (auto cli_url = args.Get("--signaling-url")) {
    client->UpdateSignalingUrl(*cli_url);
  }

  // --- ViewModel Initialization ---
  // In the new architecture, we instantiate specific ViewModels.
  auto contacts_vm = std::make_shared<eillance::view_model::ContactsViewModel>(client, router);
  auto call_vm = std::make_shared<eillance::view_model::CallViewModel>(client, router);
  auto profile_vm = std::make_shared<eillance::view_model::ProfileViewModel>(client, router);
  auto settings_vm = std::make_shared<eillance::view_model::SettingsViewModel>(client, router);

  // For conversation, we dynamically create one when a user selects a contact.
  std::shared_ptr<eillance::view_model::ConversationViewModel> conversation_vm;

  // --- Observers ---
  struct MainObserver
      : public eillance::view_model::ContactsViewModel::Observer
      , public eillance::view_model::CallViewModel::Observer
      , public eillance::view_model::SettingsViewModel::Observer
      , public eillance::view_model::ConversationViewModel::Observer {
    ftxui::ScreenInteractive& screen;
    explicit MainObserver(ftxui::ScreenInteractive& s)
        : screen(s) {
    }

    void OnContactsUpdated(std::vector<eillance::view_model::ViewContact> const&) override {
      screen.Post(ftxui::Event::Custom);
    }
    void OnCallStateChanged(eillance::view_model::CallState const&) override {
      screen.Post(ftxui::Event::Custom);
    }
    void OnSettingsUpdated(std::optional<std::string> const&, int) override {
      screen.Post(ftxui::Event::Custom);
    }
    void OnConnectionStateChanged(eillance::ServiceConnectionState) override {
      screen.Post(ftxui::Event::Custom);
    }
    void OnHistoryUpdated(eillance::view_model::ConversationViewModel::State const&) override {
      screen.Post(ftxui::Event::Custom);
    }
    void OnTransferProgress(std::string const& /*attachment_id*/, float /*progress*/) override {
      screen.Post(ftxui::Event::Custom);
    }
    // Implement Profile Observer requirement if needed or just ignore for CLI main loop
    // But since we are not observing profile changes in this CLI loop (it's mainly for chat), we can omit or add empty
    // impl if required by interface.
  };

  auto observer = std::make_shared<MainObserver>(screen);
  contacts_vm->SetObserver(observer);
  call_vm->SetObserver(observer);
  settings_vm->SetObserver(observer);

  // --- UI Components ---
  std::vector<std::string> contact_menu_entries;
  std::string input_content;
  int contact_menu_selected = 0;
  auto contact_menu = ftxui::Menu(&contact_menu_entries, &contact_menu_selected);

  auto chat_renderer = ftxui::Renderer([&]() -> ftxui::Element {
    if (!conversation_vm) {
      return ftxui::text("Select a contact to start chatting") | ftxui::center;
    }

    auto const conv_state = conversation_vm->GetState();
    auto const contacts = contacts_vm->GetContacts();
    ftxui::Elements elements;

    if (conv_state.has_older_messages) {
      elements.push_back(ftxui::text("--- older messages available (use /history load) ---") | ftxui::dim);
    }

    for (auto const& entry : conv_state.history) {
      std::string prefix;
      std::string content;
      switch (entry.kind) {
        case eillance::messaging::HistoryEntryKind::textMessage:
        case eillance::messaging::HistoryEntryKind::systemMessage: {
          std::string suffix;
          if (entry.direction == eillance::messaging::MessageDirection::sent) {
            prefix = "[you]: ";
          } else {
            auto const iter =
                std::ranges::find_if(contacts, [&](auto const& c) { return c.peer_id == conv_state.peer_id; });
            std::string const display_id =
                (iter != contacts.end()) ? iter->display_name : conv_state.peer_id.ToString();
            prefix = "[" + display_id + "]: ";
          }
          if (entry.direction == eillance::messaging::MessageDirection::sent) {
            switch (entry.status) {
              case eillance::messaging::MessageStatus::sending:
                suffix = " (sending...)";
                break;
              case eillance::messaging::MessageStatus::delivered:
                suffix = " (delivered)";
                break;
              case eillance::messaging::MessageStatus::read:
                suffix = " (read)";
                break;
              default:
                break;
            }
          }
          content = std::get<eillance::messaging::MessageEntry>(entry.data).content;
          if (entry.is_truncated) {
            content +=
                "... [truncated, ID: " + std::to_string(entry.id) + " - use /load " + std::to_string(entry.id) + "]";
          }
          auto timestamp_str = FormatTimestamp(entry.timestamp);
          elements.push_back(ftxui::text(prefix + content + suffix));
          elements.push_back(ftxui::text("  -> " + timestamp_str) | ftxui::dim);
          break;
        }
        case eillance::messaging::HistoryEntryKind::voiceCall:
        case eillance::messaging::HistoryEntryKind::videoCall: {
          prefix = "[system]: ";
          auto const& call_data = std::get<eillance::messaging::CallEntry>(entry.data);
          auto const* call_type_str =
              (entry.kind == eillance::messaging::HistoryEntryKind::voiceCall ? "Voice" : "Video");
          auto const* direction_str =
              (call_data.call_direction == eillance::messaging::CallDirection::incoming ? "Incoming" : "Outgoing");
          std::string status_str;
          switch (call_data.call_status) {
            case eillance::messaging::CallStatus::answered:
              status_str = "Answered";
              break;
            case eillance::messaging::CallStatus::missed:
              status_str = "Missed";
              break;
            case eillance::messaging::CallStatus::rejected:
              status_str = "Rejected";
              break;
            case eillance::messaging::CallStatus::busy:
              status_str = "Busy";
              break;
          }
          content = std::string("--- ") + direction_str + " " + call_type_str + " Call (" + status_str + ") ---";
          auto timestamp_str = FormatTimestamp(entry.timestamp);
          elements.push_back(ftxui::text(prefix + content) | ftxui::dim);
          elements.push_back(ftxui::text("  -> " + timestamp_str) | ftxui::dim);
          break;
        }
      }
    }
    if (!elements.empty()) {
      elements.back() |= ftxui::focus;
    }
    return ftxui::vbox(std::move(elements)) | ftxui::yframe;
  });

  auto input_component = ftxui::Input(&input_content, "Enter command or message...");
  input_component |= ftxui::CatchEvent([&](ftxui::Event const& event) {
    if (event == ftxui::Event::Return) {
      if (!input_content.empty()) {
        if (input_content.starts_with('/')) {
          std::stringstream ss(input_content);
          std::string command;
          std::string arg;
          ss >> command;
          std::getline(ss >> std::ws, arg);

          if (command == "/add") {
            try {
              contacts_vm->AddContact(eillance::ContactID(arg), std::nullopt);
            } catch (...) {
            }
          } else if (command == "/accept") {
            if (!arg.empty()) {
              contacts_vm->AcceptContact(eillance::ContactID(arg));
            }
          } else if (command == "/load" && conversation_vm) {
            try {
              conversation_vm->LoadFullMessage(std::stoull(arg));
            } catch (...) {
            }
          } else if (command == "/history" && conversation_vm) {
            conversation_vm->LoadOlderMessages();
          } else if (command == "/exit") {
            screen.Exit();
          }
        } else if (conversation_vm) {
          // Renamed method to avoid macro conflict
          conversation_vm->SendText(input_content);
        }
      }
      input_content.clear();
      return true;
    }
    return false;
  });

  auto main_container = ftxui::Container::Horizontal({contact_menu, chat_renderer, input_component});

  auto layout = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
    auto const contacts = contacts_vm->GetContacts();
    auto const call_state = call_vm->GetState();
    contact_menu_entries = GetContactMenuEntries(contacts, call_state);

    if (contact_menu_selected < contacts.size()) {
      auto const& selected_peer = contacts[contact_menu_selected].peer_id;
      if (!conversation_vm || conversation_vm->GetState().peer_id != selected_peer) {
        conversation_vm = std::make_shared<eillance::view_model::ConversationViewModel>(selected_peer, client, router);
        conversation_vm->SetObserver(observer);
      }
    } else {
      conversation_vm = nullptr;
    }

    constexpr auto k_contact_list_width = 40;
    auto const main_view = ftxui::hbox(
        {contact_menu->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, k_contact_list_width) | ftxui::border,
         ftxui::vbox({chat_renderer->Render() | ftxui::flex, ftxui::separator(), input_component->Render()}) |
             ftxui::flex}
    );

    auto const profile = profile_vm->GetProfile();
    auto const connection = settings_vm->GetState().connection_state;
    std::string const my_name = profile.my_display_name ? " (" + *profile.my_display_name + ")" : "";
    auto const status_line = " Mercury CLI | Your ID: " + profile.my_id.ToString() + my_name +
                             " | Status: " + std::string(GetConnectionStatusString(connection));
    return ftxui::window(ftxui::text(status_line), main_view);
  });

  std::thread client_thread([&] { client->Start(); });
  screen.Loop(layout);

  client->Terminate();
  if (client_thread.joinable()) {
    client_thread.join();
  }

  return 0;
}
