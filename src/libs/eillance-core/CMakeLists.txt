add_library(eillance-core STATIC
  eillance/client/client.cpp
  eillance/services/account_service.cpp
  eillance/services/contact_service.cpp
  eillance/services/messaging_service.cpp
  eillance/services/media_call_service.cpp
  eillance/view_model/contacts_view_model.cpp
  eillance/view_model/conversation_view_model.cpp
  eillance/view_model/call_view_model.cpp
  eillance/view_model/profile_view_model.cpp
  eillance/view_model/settings_view_model.cpp
  eillance/view_model/debug_view_model.cpp
)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/libplog.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")

target_include_directories(eillance-core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-core PUBLIC
  eillance-utils
  eillance-logging
  eillance-crypto
  eillance-storage-sqlite
  plog::plog
  nlohmann_json::nlohmann_json
)

if(ANDROID)
  target_link_libraries(eillance-core PUBLIC log)
endif()
