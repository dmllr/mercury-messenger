add_library(eillance-bot-api STATIC
  mercury/bot/bot.cpp
)

target_include_directories(eillance-bot-api PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-bot-api PUBLIC
  eillance-core
  eillance-rtc
)

target_compile_features(eillance-bot-api PUBLIC cxx_std_20)
