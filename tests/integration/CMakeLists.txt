add_subdirectory(mercury-mock-app)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# -s is crucial to see stdout during the build phase fixtures
add_custom_target(integration_tests
  COMMAND ${Python3_EXECUTABLE} -m pytest -v -s
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS mercury-mock-app mercury-bootstrap
  COMMENT "Running integration tests with pytest"
)

add_test(NAME IntegrationTests
  COMMAND ${Python3_EXECUTABLE} -m pytest -v -s
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
