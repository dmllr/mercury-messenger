add_subdirectory(eillance-utils)
add_subdirectory(eillance-logging)
add_subdirectory(eillance-crypto)
add_subdirectory(eillance-storage-sqlite)
add_subdirectory(eillance-h264)
add_subdirectory(eillance-core)
add_subdirectory(eillance-capture)
add_subdirectory(eillance-rtc)
add_subdirectory(eillance-bot-api)

# === Localization Generation ===
# Define paths relative to src/libs (where this file resides)
set(TRANS_SCRIPT "${REPO_ROOT}/tools/translations/generate.py")
set(TRANS_INPUT "${REPO_ROOT}/src/assets/translations")
set(TRANS_OUTPUT "${REPO_ROOT}/src/apps/android/app/src/main/res/values/strings.xml")

# Find Python
find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
  add_custom_command(
    OUTPUT "${TRANS_OUTPUT}"
    COMMAND "${Python3_EXECUTABLE}" "${TRANS_SCRIPT}"
    DEPENDS "${TRANS_SCRIPT}" "${TRANS_INPUT}/en.po" "${TRANS_INPUT}/ja.po"
    COMMENT "Generating string resources from .po files"
    VERBATIM
  )

  # Custom target allows running it manually: cmake --build . --target translations
  add_custom_target(translations DEPENDS "${TRANS_OUTPUT}")
endif()
