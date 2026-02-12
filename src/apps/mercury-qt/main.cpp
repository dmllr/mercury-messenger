#include <filesystem>
#include <memory>
#include <thread>

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "QmlStateBridge.h"
#include "eillance/capture/audio_capture.hpp"
#include "eillance/capture/video_capture.hpp"
#include "eillance/client/client.hpp"
#include "eillance/logging/logging.h"
#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "eillance/view_model/contacts_view_model.hpp"
#include "eillance/view_model/profile_view_model.hpp"

int main(int argc, char* argv[]) {
  // --- Logging Initialization ---
  static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
  Log::Initialize(&consoleAppender, plog::debug);

  // --- Application Setup ---
  QGuiApplication app(argc, argv);
  app.setOrganizationName("Eillance");
  app.setOrganizationDomain("eillance.com");
  app.setApplicationName("Mercury");

  QQmlApplicationEngine engine;

  // --- Determine Config Path ---
#ifdef _WIN32
  char const* home_dir_env = getenv("USERPROFILE");
#else
  char const* home_dir_env = getenv("HOME");
#endif
  if (home_dir_env == nullptr) {
    LGE << "Could not determine home directory.";
    return 1;
  }
  auto config_path = std::filesystem::path(home_dir_env) / ".mercury" / "Default";
  std::filesystem::create_directories(config_path);

  // --- Core Logic Initialization ---
  auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(config_path / "mercury.db");
  auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
  auto rtc_transport_impl = std::make_unique<eillance::rtc::RtcTransport>();
  auto transport = std::make_shared<eillance::transport::Transport>(std::move(rtc_transport_impl));
  auto client = std::make_shared<eillance::Client>(storage, transport);

  client->SetVideoCapture(std::make_unique<eillance::VideoCaptureCli>());
  client->SetAudioCapture(std::make_unique<eillance::AudioCaptureCli>());

  // --- Setup View Models ---
  auto router = std::make_shared<eillance::view_model::ClientEventRouter>(client);
  auto contactsVM = std::make_shared<eillance::view_model::ContactsViewModel>(client, router);
  auto profileVM = std::make_shared<eillance::view_model::ProfileViewModel>(client, router);

  if (!client->IdentityExists()) {
    // Generate new identity if none exists
    profileVM->GenerateNewIdentity();
  }

  // --- QML Bridge ---
  // The bridge takes ownership of the ViewModels logic required for QML
  auto qmlBridge = new QmlStateBridge(client, router, contactsVM, profileVM);

  engine.rootContext()->setContextProperty("StateBridge", qmlBridge);

  // This URL points to the resource created by qt_add_qml_module.
  // The format is qrc:/<URI>/<QML_FILE>
  QUrl const url(u"qrc:/mercury/qml/main.qml"_qs);
  QObject::connect(
      &engine,
      &QQmlApplicationEngine::objectCreated,
      &app,
      [url](QObject* obj, QUrl const& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
      },
      Qt::QueuedConnection
  );
  engine.load(url);

  // --- Start Client Core Logic in a Separate Thread ---
  std::thread client_thread([client] { client->Start(); });

  int app_result = app.exec();

  // --- Shutdown ---
  client->Terminate();
  if (client_thread.joinable()) {
    client_thread.join();
  }

  return app_result;
}
