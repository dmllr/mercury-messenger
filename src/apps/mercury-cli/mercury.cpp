#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <string_view>
#include <thread>
#include <vector>

#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

#include "eillance/utils/args_parser.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.h"
#include "eillance/messaging/history.hpp"
#include "eillance/rtc/client.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/view_model/mercury_view_model.hpp"
#include "eillance/capture/capture.hpp"

#include "cli_persistent_storage.hpp"
#include "video_capture_cli.hpp"

namespace {

// Helper function to format contact entries for the ftxui::Menu component.
std::vector<std::string> GetContactMenuEntries(const std::vector<eillance::view_model::UIState::ViewContact>& contacts) {
  std::vector<std::string> entries;
  for (const auto& contact: contacts) {
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
        case rtc::PeerConnection::State::Connecting: conn_status_str = " (Connecting)"; break;
        case rtc::PeerConnection::State::Connected: conn_status_str = " (Connected)"; break;
        case rtc::PeerConnection::State::Failed: conn_status_str = " (Failed)"; break;
        default: break;
    }

    const std::string display_id = contact.peer_id.length() > 12 ? contact.peer_id.substr(0, 12) + "..." : contact.peer_id;
    entries.push_back(display_id + status_str + conn_status_str);
  }
  return entries;
}

constexpr auto GetConnectionStatusString(eillance::ServiceConnectionState state) -> std::string_view {
  switch (state) {
    case eillance::ServiceConnectionState::establishing: return "CONNECTING";
    case eillance::ServiceConnectionState::online: return "ONLINE";
    case eillance::ServiceConnectionState::failure: return "CONNECTION FAILED";
    case eillance::ServiceConnectionState::disconnected: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

// Helper function to format timestamps according to the specified rules.
std::string FormatTimestamp(Time::Unit ts_us) {
    using namespace std::chrono;
    const auto message_tp = system_clock::time_point(microseconds(ts_us));
    const auto now_tp = system_clock::now();

    const time_t message_time_t = system_clock::to_time_t(message_tp);
    const time_t now_time_t = system_clock::to_time_t(now_tp);

#ifdef _WIN32
    tm message_tm{}, now_tm{};
    localtime_s(&message_tm, &message_time_t);
    localtime_s(&now_tm, &now_time_t);
#else
    tm message_tm{}, now_tm{};
    localtime_r(&message_time_t, &message_tm);
    localtime_r(&now_time_t, &now_tm);
#endif

    char buffer[64];

    if (message_tm.tm_year == now_tm.tm_year && message_tm.tm_yday == now_tm.tm_yday) {
        strftime(buffer, sizeof(buffer), "%H:%M", &message_tm);
        return std::string(buffer);
    }

    time_t yesterday_time_t = now_time_t - 86400;
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

  const ArgsParser args(argc, argv);
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
  // Redirect logs to a file to prevent interference with the UI.
  auto log_file_path = config_path / "mercury-cli.log";
  std::filesystem::create_directories(log_file_path.parent_path());
  const auto u8_path = log_file_path.u8string();
  const auto log_file_path_str{std::string{reinterpret_cast<const char*>(u8_path.c_str()), u8_path.length()}};
  static plog::RollingFileAppender<plog::TxtFormatter> file_appender(log_file_path_str.c_str(), 5 * 1024 * 1024, 3);
  Log::Initialize(&file_appender, plog::info);

  // --- Service Layer (Model) Initialization ---
  auto storage_impl = std::make_unique<eillance::storage::CliPersistentStorageImpl>(config_path);
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
  auto video_capture = eillance::Capture(std::make_unique<eillance::VideoCaptureCli>());
  auto rtc_client = std::make_shared<eillance::RtcClient>(storage, std::move(video_capture));

  // --- ViewModel Initialization ---
  auto view_model = std::make_unique<eillance::view_model::MercuryViewModel>(rtc_client);

  // Connect UI to ViewModel. The ViewModel will notify the UI when to redraw.
  view_model->SetOnStateChangedCallback([&] { screen.Post(ftxui::Event::Custom); });

  // --- UI Components ---
  std::vector<std::string> contact_menu_entries;
  std::string input_content;
  auto contact_menu_selected = 0;
  auto contact_menu = ftxui::Menu(&contact_menu_entries, &contact_menu_selected);

  auto chat_renderer = ftxui::Renderer([&]() -> ftxui::Element {
    const auto state = view_model->GetState();
    ftxui::Elements elements;

    if (state.has_older_messages) {
        elements.push_back(ftxui::text("--- older messages available (use /history load) ---") | ftxui::dim);
    }

    for (const auto& entry: state.interaction_history) {
        std::string prefix;
        std::string content;
        switch (entry.kind) {
            case eillance::messaging::HistoryEntryKind::textMessage:
            case eillance::messaging::HistoryEntryKind::systemMessage: {
            std::string suffix;
            if (entry.direction == eillance::messaging::MessageDirection::sent) {
                    prefix = "[you]: ";
                } else {
                    if(state.selected_contact_id.has_value()) {
                        const auto& peer_id = state.selected_contact_id.value();
                        const std::string display_id = peer_id.length() > 8 ? peer_id.substr(0, 8) + "..." : peer_id;
                        prefix = "[" + display_id + "]: ";
                    } else {
                        prefix = "[system]: ";
                    }
                }
                if (entry.direction == eillance::messaging::MessageDirection::sent) {
                    switch(entry.status) {
                        case eillance::messaging::MessageStatus::sending: suffix = " (sending...)"; break;
                        case eillance::messaging::MessageStatus::sent: suffix = " (sent)"; break;
                        case eillance::messaging::MessageStatus::delivered: suffix = " (delivered)"; break;
                        case eillance::messaging::MessageStatus::read: suffix = " (read)"; break;
                        default: break;
                    }
                }
                content = std::get<eillance::messaging::MessageEntry>(entry.data).content;
                auto timestamp_str = FormatTimestamp(entry.timestamp);
                elements.push_back(ftxui::text(prefix + content + suffix));
                elements.push_back(ftxui::text("  ΓööΓöÇ " + timestamp_str) | ftxui::dim);
                break;
            }
            case eillance::messaging::HistoryEntryKind::voiceCall:
            case eillance::messaging::HistoryEntryKind::videoCall: {
                prefix = "[system]: ";
                const auto& call_data = std::get<eillance::messaging::CallEntry>(entry.data); // NOLINT
                std::string call_type_str = (entry.kind == eillance::messaging::HistoryEntryKind::voiceCall ? "Voice" : "Video");
                std::string direction_str = (call_data.call_direction == eillance::messaging::CallDirection::incoming ? "Incoming" : "Outgoing");
                std::string status_str;
                switch (call_data.call_status) {
                    case eillance::messaging::CallStatus::answered: status_str = "Answered"; break;
                    case eillance::messaging::CallStatus::missed: status_str = "Missed"; break;
                    case eillance::messaging::CallStatus::rejected: status_str = "Rejected"; break;
                    case eillance::messaging::CallStatus::busy: status_str = "Busy"; break;
                }
                content = "--- " + direction_str + " " + call_type_str + " Call (" + status_str + ") ---";
                auto timestamp_str = FormatTimestamp(entry.timestamp);
                elements.push_back(ftxui::text(prefix + content) | ftxui::dim);
                elements.push_back(ftxui::text("  ΓööΓöÇ " + timestamp_str) | ftxui::dim);
                break;
            }
        }
    }
    // Add a focusable invisible element at the end to scroll down automatically.
    if (!elements.empty()) {
        elements.back() |= ftxui::focus;
    }
    return ftxui::vbox(std::move(elements)) | ftxui::yframe;
  });

  auto input_component = ftxui::Input(&input_content, "Enter command or message...");
  // The input component only forwards the user input to the ViewModel.
  input_component |= ftxui::CatchEvent([&](const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
      if (view_model->ProcessCliInput(input_content)) {
        screen.Exit();
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

    const auto main_view = ftxui::hbox({
        contact_menu->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::border,
        ftxui::vbox({
            chat_renderer->Render() | ftxui::flex,
            ftxui::separator(),
            input_component->Render()
        }) | ftxui::flex
    });

    const auto status_line = " Mercury CLI | Your ID: " + state.my_id + " | Status: " + std::string(GetConnectionStatusString(state.connection_state));
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
