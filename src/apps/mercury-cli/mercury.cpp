#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

#include "hela/eillance/events/service_connection_state.h"
#include "hela/eillance/identity/contact_manager.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/client.hpp"
#include "hela/eillance/storage/persistent_storage.hpp"
#include "hela/eillance/view_model/mercury_view_model.hpp"

namespace {

// Helper function to format contact entries for the ftxui::Menu component.
std::vector<std::string> GetContactMenuEntries(const std::vector<hela::eillance::identity::Contact>& contacts) {
  std::vector<std::string> entries;
  for (const auto& contact: contacts) {
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
    entries.push_back(display_id + status_str);
  }
  return entries;
}

auto GetConnectionStatusString(hela::eillance::ServiceConnectionState state) -> std::string {
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

  // --- Service Layer (Model) Initialization ---
  hela::eillance::storage::Config storage_config;
  storage_config.platform_config = config_path;
  auto rtc_client = std::make_shared<hela::eillance::RtcClient>(storage_config);

  // --- ViewModel Initialization ---
  auto view_model = std::make_unique<hela::eillance::view_model::MercuryViewModel>(rtc_client);

  // Connect UI to ViewModel. The ViewModel will notify the UI when to redraw.
  view_model->setOnStateChangedCallback([&] { screen.Post(ftxui::Event::Custom); });

  // --- UI Components ---
  std::vector<std::string> contact_menu_entries;
  std::string input_content;
  auto contact_menu_selected = 0;
  auto contact_menu = ftxui::Menu(&contact_menu_entries, &contact_menu_selected);

  auto chat_renderer = ftxui::Renderer([&]() -> ftxui::Element {
    // The chat history is fetched from the ViewModel on each render.
    auto state = view_model->getState();
    ftxui::Elements elements;
    for (const auto& line: state.chat_history) {
      elements.push_back(ftxui::text(line));
    }
    return ftxui::vbox(std::move(elements)) | ftxui::frame | ftxui::flex_grow;
  });

  auto input_component = ftxui::Input(&input_content, "Enter command or message...");
  // The input component only forwards the user input to the ViewModel.
  input_component |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
      if (view_model->processUserInput(input_content)) {
        screen.Exit();
      }
      input_content.clear();
      return true;
    }
    return false;
  });

  auto main_container = ftxui::Container::Horizontal({contact_menu, input_component});

  // The main layout renderer, responsible for drawing the entire screen.
  auto layout = ftxui::Renderer(main_container, [&]() -> ftxui::Element {
    // Fetch the latest state from the ViewModel for rendering.
    auto state = view_model->getState();

    // Update local UI structures needed by components.
    contact_menu_entries = GetContactMenuEntries(state.contacts);

    // Inform the ViewModel of the currently selected item in the UI.
    view_model->selectContactByIndex(contact_menu_selected);

    auto main_view = ftxui::hbox({contact_menu->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::border,
                                  ftxui::vbox({chat_renderer->Render(),
                                               ftxui::separator(),
                                               input_component->Render() | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1)}) |
                                      ftxui::flex_grow});

    const std::string status_line = " Mercury CLI | Your ID: " + state.my_id + " | Status: " + GetConnectionStatusString(state.connection_state);
    return ftxui::window(ftxui::text(status_line), main_view);
  });

  // Start the service layer's background processes and the UI event loop.
  std::thread rtc_thread([&] { rtc_client->Start(); });
  screen.Loop(layout);

  // Cleanly shut down the service layer.
  rtc_client->Terminate();
  if (rtc_thread.joinable()) {
    rtc_thread.join();
  }

  return 0;
}
