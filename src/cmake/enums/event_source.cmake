set(event_source_enum_values
  System
  RtcPeerConnection
  RtcGathering
  Rtsp
)

# Generate C++ header file
set(output_cpp "${CMAKE_BINARY_DIR}/generated/enums/event_source.h")
file(WRITE ${output_cpp} "#pragma once\n")
file(APPEND ${output_cpp} "#include <cstdint>\n")
file(APPEND ${output_cpp} "enum class EventSource : std::uint8_t {\n")
foreach(value IN LISTS event_source_enum_values)
  file(APPEND ${output_cpp} "  ${value},\n")
endforeach()
file(APPEND ${output_cpp} "};\n")

if (ANDROID)
  # Generate Kotlin enum file
  set(output_kotlin "${AP_GENERATED_SOURCES_DIR_NAME}/EventSource.kt")
  file(WRITE ${output_kotlin} "enum class EventSource(val value: Int) {\n")
  foreach(value IN LISTS event_source_enum_values)
    file(APPEND ${output_kotlin} "  ${value},\n")
  endforeach()
  file(APPEND ${output_kotlin} "}\n")
endif()
