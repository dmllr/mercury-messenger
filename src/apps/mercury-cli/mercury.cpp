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
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

#include "eillance/capture/audio_capture.hpp"
#include "eillance/capture/video_capture.hpp"
#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/logging/logging.h"
#include "eillance/rtc/transport_impl_libdatachannel.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/utils/args_parser.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/view_model/mercury_view_model.hpp"

namespace {

// Helper function to format contact entries for the ftxui::Menu component.
std::vector<std::string> GetContactMenuEntries(const std::vector<eillance::view_model::UIState::ViewContact>& contacts) {
  std::vector<std::string> entries;
  for (const auto& contact : contacts) {
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
    }

    std::string conn_status_str;
    switch (contact.connection_state) {
      case eillance::transport::PeerConnectionState::Connecting:
        conn_status_str = " (Connecting)";
        break;
      case eillance::transport::PeerConnectionState::Connected:
        conn_status_str = " (Connected)";
        break;
      case eillance::transport::PeerConnectionState::Failed:
        conn_status_str = " (Failed)";
        break;
      default:
        break;
    }

    std::string unread_count_str;
    if (contact.unread_count > 0) { unread_count_str = " [" + std::to_string(contact.unread_count) + "]"; }

    constexpr auto max_id_display_length = 12;
    entries.push_back(contact.display_name + status_str + conn_status_str + unread_count_str);
  }
  return entries;
}

constexpr auto GetConnectionStatusString(eillance::ServiceConnectionState state) -> std::string_view {
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
std::string FormatTimestamp(Time::Unit ts_seconds) {
  using namespace std::chrono;
  const auto message_tp = system_clock::time_point(seconds(ts_seconds));
  const auto now_tp = system_clock::now();

  const time_t message_time_t = system_clock::to_time_t(message_tp);
  const time_t now_time_t = system_clock::to_time_t(now_tp);

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

  constexpr auto buffer_size = 64;
  char buffer[buffer_size];

  if (message_tm.tm_year == now_tm.tm_year && message_tm.tm_yday == now_tm.tm_yday) {
    strftime(buffer, sizeof(buffer), "%H:%M", &message_tm);
    return std::string(buffer);
  }

  constexpr auto seconds_in_day = 86400;
  time_t yesterday_time_t = now_time_t - seconds_in_day;
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

int main(int argc, char** argv) {
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  const eillance::utils::ArgsParser args(argc, argv);
  const std::string config_name_str = args.GetPositional(0).value_or("Default");
  const char* config_name = config_name_str.c_str();

#ifdef _WIN32
  const char* home_dir_env = getenv("USERPROFILE");
#else
  const char* home_dir_env = getenv("HOME");
#endif
  if (home_dir_env == nullptr) {
    std::cerr << "Could not determine home directory." << std::endl;
    return 1;
  }
  auto config_path = std::filesystem::path(home_dir_env) / ".mercury" / config_name;

  // --- Logging Initialization ---
  auto log_file_path = config_path / "mercury-cli.log";
  std::filesystem::create_directories(log_file_path.parent_path());
  const auto u8_path = log_file_path.u8string();
  const auto log_file_path_str{std::string{reinterpret_cast<const char*>(u8_path.c_str()), u8_path.length()}};
  constexpr auto max_log_size_bytes = 5 * 1024 * 1024;
  constexpr auto max_log_files = 3;
  static plog::RollingFileAppender<plog::TxtFormatter> file_appender(log_file_path_str.c_str(), max_log_size_bytes, max_log_files);
  Log::Initialize(&file_appender, plog::info);

  // --- Service Layer (Model) Initialization ---
  auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(config_path / "mercury.db");
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));

  // --- Dependency Injection & Client Initialization ---
  auto db_path = (config_path / "mercury.db").string();
  auto rtc_transport_impl = std::make_unique<eillance::rtc::RtcTransport>();
  auto transport = std::make_shared<eillance::transport::Transport>(std::move(rtc_transport_impl));
  auto client = std::make_shared<eillance::Client>(storage, transport);

  // --- Setter Injection for Platform-Specific Implementations ---
  client->SetVideoCapture(std::make_unique<eillance::VideoCaptureCli>());
  client->SetAudioCapture(std::make_unique<eillance::AudioCaptureCli>());

  if (auto backup_path = args.Get("--backup")) {
      std::cout << "Enter password for backup: ";
      std::string password;
      std::getline(std::cin, password);
      if (client->CreateBackup(*backup_path, password)) {
          std::cout << "Backup created successfully at " << *backup_path << std::endl;
      } else {
          std::cerr << "Backup failed." << std::endl;
      }
      return 0;
  }

  if (auto restore_path = args.Get("--restore")) {
      std::cout << "Enter password for backup file: ";
      std::string password;
      std::getline(std::cin, password);
      std::cout << "WARNING: This will overwrite your current identity, contacts, and messages." << std::endl;
      std::cout << "Type 'yes' to continue: ";
      std::string confirmation;
      std::getline(std::cin, confirmation);
      if (confirmation == "yes") {
          if (client->RestoreBackup(*restore_path, password)) {
              std::cout << "Restore successful. Please restart the application." << std::endl;
          } else {
              std::cerr << "Restore failed. Check password or file integrity." << std::endl;
          }
      } else {
          std::cout << "Restore cancelled." << std::endl;
      }
      return 0;
  }

  if (!client->IdentityExists()) {
      std::cout << "Welcome to Mercury! No identity found.\n"
                << "1. Create a new identity\n"
                << "2. Restore from a 12-word recovery phrase\n"
                << "Enter your choice: ";
      int choice;
      std::cin >> choice;
      std::cin.ignore(); 
      if (choice == 1) {
          auto mnemonic = client->GenerateNewIdentity();
          std::cout << "\nIMPORTANT: Save this recovery phrase securely:\n\n" << mnemonic << "\n\nPress Enter to continue...";
          std::cin.get();
      } else if (choice == 2) {
          std::cout << "Enter your 12-word recovery phrase: ";
          std::string phrase;
          std::getline(std::cin, phrase);
          if (!client->RestoreIdentity(phrase)) { std::cerr << "Invalid phrase." << std::endl; return 1; }
      } else { std::cerr << "Invalid choice." << std::endl; return 1; }
      std::cout << "Identity configured. Starting Mercury..." << std::endl;
  }

  // Handle command-line override for signaling URL before starting the client
  if (auto cli_url = args.Get("--signaling-url")) {
    client->UpdateSignalingUrl(*cli_url);
  }

  // --- ViewModel Initialization ---
  auto view_model = std::make_unique<eillance::view_model::MercuryViewModel>(client);

  view_model->SetOnStateChangedCallback([&] { screen.Post(ftxui::Event::Custom); });

  // --- UI Components ---
  std::vector<std::string> contact_menu_entries;
  std::string input_content;
  auto contact_menu_selected = 0;
  auto contact_menu = ftxui::Menu(&contact_menu_entries, &contact_menu_selected);

  auto chat_renderer = ftxui::Renderer([&]() -> ftxui::Element {
    const auto state = view_model->GetState();
    ftxui::Elements elements;

    if (state.has_older_messages) { elements.push_back(ftxui::text("--- older messages available (use /history load) ---") | ftxui::dim); }

    for (const auto& entry : state.interaction_history) {
      std::string prefix;
      std::string content;
      switch (entry.kind) {
        case eillance::messaging::HistoryEntryKind::textMessage:
        case eillance::messaging::HistoryEntryKind::systemMessage: {
          std::string suffix;
          if (entry.direction == eillance::messaging::MessageDirection::sent) {
            prefix = "[you]: ";
          } else {
            if (state.selected_contact_id.has_value()) {
              const auto it = std::ranges::find_if(state.contacts, [&](const auto& c){ return c.peer_id == *state.selected_contact_id; });
              const std::string display_id = (it != state.contacts.end()) ? it->display_name : *state.selected_contact_id;
              prefix = "[" + display_id + "]: ";
            } else {
              prefix = "[system]: ";
            }
          }
          if (entry.direction == eillance::messaging::MessageDirection::sent) {
            switch (entry.status) {
              case eillance::messaging::MessageStatus::sending:
                suffix = " (sending...)";
                break;
              case eillance::messaging::MessageStatus::sent:
                suffix = " (sent)";
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
            content += "... [truncated, ID: " + std::to_string(entry.id) + " - use /load " + std::to_string(entry.id) + "]";
          }
          auto timestamp_str = FormatTimestamp(entry.timestamp);
          elements.push_back(ftxui::text(prefix + content + suffix));
          elements.push_back(ftxui::text("  → " + timestamp_str) | ftxui::dim);
          break;
        }
        case eillance::messaging::HistoryEntryKind::voiceCall:
        case eillance::messaging::HistoryEntryKind::videoCall: {
          prefix = "[system]: ";
          const auto& call_data = std::get<eillance::messaging::CallEntry>(entry.data);  // NOLINT
          std::string call_type_str = (entry.kind == eillance::messaging::HistoryEntryKind::voiceCall ? "Voice" : "Video");
          std::string direction_str = (call_data.call_direction == eillance::messaging::CallDirection::incoming ? "Incoming" : "Outgoing");
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
          content = "--- " + direction_str + " " + call_type_str + " Call (" + status_str + ") ---";
          auto timestamp_str = FormatTimestamp(entry.timestamp);
          elements.push_back(ftxui::text(prefix + content) | ftxui::dim);
          elements.push_back(ftxui::text("  → " + timestamp_str) | ftxui::dim);
          break;
        }
      }
    }
    // Add a focusable invisible element at the end to scroll down automatically.
    if (!elements.empty()) { elements.back() |= ftxui::focus; }
    return ftxui::vbox(std::move(elements)) | ftxui::yframe;
  });

  auto input_component = ftxui::Input(&input_content, "Enter command or message...");
  // The input component only forwards the user input to the ViewModel.
  input_component |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
      if (!input_content.empty()) {
        if (input_content.starts_with('/')) {
          std::stringstream ss(input_content);
          std::string command;
          std::string arg;
          ss >> command;
          std::getline(ss >> std::ws, arg);

          if (command == "/add") {
            view_model->AddSystemMessageToChat("--> Adding contact: " + arg);
            view_model->AddContact(arg);
          } else if (command == "/accept") {
            if (arg.empty()) {
              view_model->AddSystemMessageToChat("--> Usage: /accept <contact_id>");
            } else {
              std::vector<std::string> matches;
              const auto state = view_model->GetState();
              for (const auto& contact : state.contacts) {
                if (contact.peer_id.starts_with(arg)) { matches.push_back(contact.peer_id); }
              }
              if (matches.empty()) {
                view_model->AddSystemMessageToChat("--> Contact not found for ID starting with: " + arg);
              } else if (matches.size() > 1) {
                view_model->AddSystemMessageToChat("--> Ambiguous ID. Multiple contacts start with: " + arg);
              } else {
                view_model->AddSystemMessageToChat("--> Accepting contact: " + matches[0]);
                view_model->AcceptContact(matches[0]);
              }
            }
          } else if (command == "/set") {
            std::string key;
            std::string value;
            std::stringstream arg_ss(arg);
            arg_ss >> key;
            std::getline(arg_ss >> std::ws, value);
            if (key == "name") {
              view_model->UpdateMyProfile(value, std::nullopt, std::nullopt);
              view_model->AddSystemMessageToChat("--> Display name updated.");
            } else if (key == "signaling_server_url") {
              view_model->UpdateSignalingUrl(value);
              view_model->AddSystemMessageToChat("--> Signaling server URL updated. Reconnecting...");
            } else {
              view_model->AddSystemMessageToChat("--> Unknown setting: " + key);
            }
          } else if (command == "/history" && arg == "load") {
            view_model->LoadOlderMessages();
          } else if (command == "/load") {
            try {
              if (arg.empty()) { throw std::invalid_argument("Message ID required."); }
              const uint64_t id = std::stoull(arg);
              view_model->LoadFullMessage(id);
              view_model->AddSystemMessageToChat("--> Loading full content for message " + arg);
            } catch (const std::exception& e) {
              view_model->AddSystemMessageToChat(std::string("--> Invalid usage. Use /load <message_id>. Error: ") + e.what());
            }
          } else if (command == "/myid") {
            const auto state = view_model->GetState();
            view_model->AddSystemMessageToChat("--> Your ID: " + state.my_id);
          } else if (command == "/exit") {
            screen.Exit();
          } else {
            view_model->AddSystemMessageToChat("--> Unknown command: " + command);
          }
        } else {
          view_model->SendMessage(input_content);
        }
      }
      input_content.clear();
      return true;
    }
    return false;
  });

  auto main_container = ftxui::Container::Horizontal({contact_menu, chat_renderer, input_component});

  // The main layout renderer, responsible for drawing the entire screen.
  auto layout = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
    // Fetch the latest state from the ViewModel for rendering.
    const auto state = view_model->GetState();

    // Update local UI structures needed by components.
    contact_menu_entries = GetContactMenuEntries(state.contacts);

    // Inform the ViewModel of the currently selected item in the UI.
    view_model->SelectContactByIndex(contact_menu_selected);

    constexpr auto contact_list_width = 40;
    const auto main_view = ftxui::hbox({contact_menu->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, contact_list_width) | ftxui::border, ftxui::vbox({chat_renderer->Render() | ftxui::flex, ftxui::separator(), input_component->Render()}) | ftxui::flex});

    const std::string my_name = state.my_display_name ? " (" + *state.my_display_name + ")" : "";
    const auto status_line = " Mercury CLI | Your ID: " + state.my_id + my_name + " | Status: " + std::string(GetConnectionStatusString(state.connection_state));
    return ftxui::window(ftxui::text(status_line), main_view);
  });

  std::thread client_thread([&] { client->start(); });
  screen.Loop(layout);

  client->terminate();
  if (client_thread.joinable()) { client_thread.join(); }

  return 0;
}
