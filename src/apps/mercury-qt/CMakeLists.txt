cmake_minimum_required(VERSION 3.16)

project(mercury-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

# --- Qt6 Location Hints ---
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.10.0/msvc2022_64")
elseif(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.10.0/macos")
else()
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.10.0/gcc_64")
endif()

# Find Qt6
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick)

# Setup paths and include shared cmake modules from the project
include("${shared_cmake_libs_path}/setup.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libopencv.cmake")
include("${shared_cmake_libs_path}/libffmpeg.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")

qt_standard_project_setup()

# Define the executable
qt_add_executable(mercury-qt
  main.cpp
  QmlStateBridge.h
  QmlStateBridge.cpp
)

# This command creates a QML module named "mercury". It compiles the QML files
# into the executable's resources and generates the necessary metadata so that
# "import mercury 1.0" works correctly in other QML files.
qt_add_qml_module(mercury-qt
  URI mercury
  VERSION 1.0
  QML_FILES
  qml/main.qml
  qml/ContactListView.qml
  qml/AddContactDialog.qml
  qml/ConversationView.qml
)

# Link against Qt modules
target_link_libraries(mercury-qt PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
)

# Link against our existing core libraries
target_link_libraries(mercury-qt PRIVATE
  eillance-core
  eillance-rtc
  libopencv
  libffmpeg
  libminiaudio
  opus
)

install(TARGETS mercury-qt
  BUNDLE DESTINATION .
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
