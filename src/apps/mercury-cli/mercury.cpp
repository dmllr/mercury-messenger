#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"

#include "hela/eillance/events/service_connection_state.h"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/client.hpp"

// --- Global State (for UI interaction) ---
std::mutex ui_mutex;
std::vector<hela::eillance::identity::Contact> contact_list;
std::vector<std::string> contact_menu_entries;
std::string selected_contact_id;
std::deque<std::string> chat_history;
std::string input_content;
std::atomic<hela::eillance::ServiceConnectionState> service_connection_state = hela::eillance::ServiceConnectionState::unknown;

namespace {

void UpdateContactEntries() {
  contact_menu_entries.clear();
  for (const auto& contact: contact_list) {
    std::string status_str;
    switch (contact.status) {
      case hela::eillance::identity::ContactStatus::PendingApproval:
        status_str = " [pending]";
        break;
      case hela::eillance::identity::ContactStatus::AwaitingApproval:
        status_str = " [ACTION REQUIRED]";
        break;
      case hela::eillance::identity::ContactStatus::Confirmed:
        status_str = "";
        break;
    }
    const std::string display_id = contact.peer_id.length() > 12 ? contact.peer_id.substr(0, 12) + "..." : contact.peer_id;
    contact_menu_entries.push_back(display_id + status_str);
  }
}

std::string GetConnectionStatusString(hela::eillance::ServiceConnectionState state) {
    switch (state) {
        case hela::eillance::ServiceConnectionState::establishing: return "CONNECTING";
        case hela::eillance::ServiceConnectionState::online: return "ONLINE";
        case hela::eillance::ServiceConnectionState::failure: return "CONNECTION FAILED";
        case hela::eillance::ServiceConnectionState::disconnected: return "OFFLINE";
        default: return "UNKNOWN";
    }
}

}  // namespace

int main(int argc, char** argv) {
  auto screen = ftxui::ScreenInteractive::Fullscreen();

  // --- Backend Initialization ---
  auto* config_name = "Default";
  if (argc > 1) {
    config_name = argv[1];
  }

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
  // Redirect logs to a file to prevent interference with the UI.
  auto log_file_path = config_path / "mercury-cli.log";
  std::filesystem::create_directories(log_file_path.parent_path());
  auto u8_path = log_file_path.u8string();
  std::string log_file_path_str(reinterpret_cast<const char*>(u8_path.c_str()), u8_path.length());
  static plog::RollingFileAppender<plog::TxtFormatter> file_appender(log_file_path_str.c_str(), 5 * 1024 * 1024, 3);
  Log::Initialize(&file_appender, plog::info);

  hela::eillance::storage::Config storage_config;
  storage_config.platform_config = config_path;
  auto rtc_client = std::make_shared<hela::eillance::RtcClient>(storage_config);
  std::string my_id = rtc_client->GetId();

  // --- Initial State Load ---
  {
    const std::lock_guard<std::mutex> lock(ui_mutex);
    contact_list = rtc_client->GetContacts();
    UpdateContactEntries();
  }

  // --- UI Callbacks from Backend ---
  rtc_client->SetChatMessageReceivedCallback([&](const std::string& peer_id, const std::string& message) {
    const std::lock_guard<std::mutex> lock(ui_mutex);
    if (peer_id == selected_contact_id) {
      const std::string display_id = peer_id.length() > 8 ? peer_id.substr(0, 8) + "..." : peer_id;
      chat_history.push_back("[" + display_id + "]: " + message);
      if (chat_history.size() > 50) {
        chat_history.pop_front();
      }
      screen.Post(ftxui::Event::Custom);
    }
  });

  rtc_client->SetContactRequestCallback([&](const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(ui_mutex);
    chat_history.push_back("--> Contact request from " + peer_id);
    chat_history.push_back("--> Use '/accept " + peer_id + "' to confirm.");
    contact_list = rtc_client->GetContacts();
    UpdateContactEntries();
    screen.Post(ftxui::Event::Custom);
  });

  rtc_client->SetContactStatusChangedCallback([&](const std::string& peer_id, hela::eillance::identity::ContactStatus status) {
    const std::lock_guard<std::mutex> lock(ui_mutex);
    const std::string display_id = peer_id.length() > 12 ? peer_id.substr(0, 12) + "..." : peer_id;
    chat_history.push_back("--> Contact status for " + display_id + " updated.");
    contact_list = rtc_client->GetContacts();
    UpdateContactEntries();
    screen.Post(ftxui::Event::Custom);
  });

  rtc_client->SetStatusCallback([&](const std::string& /*peer_id*/, hela::eillance::EventSource source, int state_val) {
      if (source == hela::eillance::EventSource::system) {
          auto state = static_cast<hela::eillance::ServiceConnectionState>(state_val);
          service_connection_state = state;
          screen.Post(ftxui::Event::Custom); // Refresh UI
      }
  });


  // --- UI Components ---
  auto contact_menu_selected = 0;
  auto contact_menu = ftxui::Menu(&contact_menu_entries, &contact_menu_selected);

  auto chat_renderer = ftxui::Renderer([&]() -> ftxui::Element {
    ftxui::Elements elements;
    for (const auto& line: chat_history) {
      elements.push_back(ftxui::text(line));
    }
    return ftxui::vbox(std::move(elements)) | ftxui::frame | ftxui::flex_grow;
  });

  auto input_component = ftxui::Input(&input_content, "Enter command or message...");
  input_component |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
      const std::string cmd = input_content;
      input_content.clear();

      std::function<void()> rtc_action;

      {  // Single lock scope for all UI updates.
        const std::lock_guard<std::mutex> lock(ui_mutex);

        if (cmd.starts_with("/")) {  // Command processing.
          std::stringstream ss(cmd);
          std::string command;
          std::string arg;
          ss >> command;
          std::getline(ss >> std::ws, arg);

          if (command == "/add") {
            chat_history.push_back("--> Adding contact: " + arg);
            // Defer the RTC action to be executed after the lock is released.
            rtc_action = [rtc_client, arg] { rtc_client->AddContact(arg); };
          } else if (command == "/accept") {
            chat_history.push_back("--> Accepting contact: " + arg);
            rtc_action = [rtc_client, arg] { rtc_client->ConfirmContact(arg); };
          } else if (command == "/exit") {
            rtc_action = [&] { screen.Exit(); };
          } else {
            chat_history.push_back("--> Unknown command: " + command);
          }
        } else {  // Message processing.
          if (selected_contact_id.empty()) {
            chat_history.emplace_back("--> Select a contact before sending a message.");
          } else if (!cmd.empty()) {
            chat_history.emplace_back("[you]: " + cmd);
            // Capture necessary data for the action.
            const std::string peer_id = selected_contact_id;
            rtc_action = [rtc_client, peer_id, cmd] { rtc_client->SendChatMessage(peer_id, cmd); };
          }
        }

        if (chat_history.size() > 50) {
          chat_history.pop_front();
        }
      }  // Mutex is released here.

      // Execute the deferred RTC action, if any.
      if (rtc_action) {
        rtc_action();
      }

      return true;
    }
    return false;
  });

  // --- UI Layout ---
  auto main_container = ftxui::Container::Horizontal({contact_menu, input_component});

  auto layout = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
    const std::lock_guard<std::mutex> lock(ui_mutex);
    if (!contact_list.empty() && contact_menu_selected >= 0 && contact_menu_selected < contact_list.size()) {
      selected_contact_id = contact_list[contact_menu_selected].peer_id;
    } else {
      selected_contact_id.clear();
    }

    auto main_view = ftxui::hbox({contact_menu->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::border,
                                  ftxui::vbox({chat_renderer->Render(),
                                               ftxui::separator(),
                                               input_component->Render() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1)}) |
                                      ftxui::flex_grow});

    std::string status_line = " Mercury CLI | Your ID: " + my_id + " | Status: " + GetConnectionStatusString(service_connection_state.load());
    return ftxui::window(ftxui::text(status_line), main_view);
  });

  // --- Start event loop and backend ---
  std::thread rtc_thread([&] {
      rtc_client->Start();
  });

  screen.Loop(layout);

  rtc_client->Terminate();
  if (rtc_thread.joinable()) {
    rtc_thread.join();
  }

  return 0;
}
