# C64 Stream Plugin Tests
#
# This directory contains the following test components:
# - test_vic_colors.c: Unit tests for VIC-II color conversion (local builds only)
# - c64_mock_server.c: Mock C64 Ultimate device for testing plugin connectivity (local builds only)
# - test_integration.c: Full integration tests with real OBS (disabled by default)
# - CMakeLists.txt: This build configuration with automatic CI detection
#
# Build behavior:
# - Local development: Builds plugin + tests + mock server
# - CI environment: Builds plugin only (all tests disabled)
#
cmake_minimum_required(VERSION 3.28)

project(c64stream-tests)

# Enable testing
enable_testing()

# Find required packages
find_package(Threads REQUIRED)

# Detect CI environment and set defaults accordingly
set(IS_CI_BUILD OFF)
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
  set(IS_CI_BUILD ON)
endif()

# Set defaults: enable mock server for local development, disable for CI
# Integration tests are disabled by default due to OBS header conflicts
if(IS_CI_BUILD)
  set(DEFAULT_ENABLE_MOCK_SERVER OFF)
  set(DEFAULT_ENABLE_INTEGRATION_TESTS OFF)
else()
  set(DEFAULT_ENABLE_MOCK_SERVER ON)
  set(DEFAULT_ENABLE_INTEGRATION_TESTS OFF) # Disabled by default due to OBS header conflicts
endif()

# Options for controlling what gets built
option(ENABLE_MOCK_SERVER "Build the C64 Ultimate mock server for local testing" ${DEFAULT_ENABLE_MOCK_SERVER})
option(
  ENABLE_INTEGRATION_TESTS
  "Build integration tests (requires refactoring to avoid OBS header conflicts)"
  ${DEFAULT_ENABLE_INTEGRATION_TESTS}
)

# Automatically enable mock server if integration tests are enabled
if(ENABLE_INTEGRATION_TESTS AND NOT ENABLE_MOCK_SERVER)
  message(STATUS "Integration tests enabled - automatically enabling mock server")
  set(ENABLE_MOCK_SERVER ON CACHE BOOL "Build the C64 Ultimate mock server for local testing" FORCE)
endif()

# Show what's being built
message(STATUS "Test configuration:")
message(STATUS "  CI Build: ${IS_CI_BUILD}")
message(STATUS "  Mock Server: ${ENABLE_MOCK_SERVER}")
message(STATUS "  Integration Tests: ${ENABLE_INTEGRATION_TESTS}")

# VIC color unit tests - only build locally (not in CI)
if(NOT IS_CI_BUILD)
  add_executable(test_vic_colors test_vic_colors.c)
  add_test(NAME VICColors COMMAND test_vic_colors)
endif()

# C64 mock server (for manual testing) - only build when explicitly enabled
# Note: Requires POSIX threads (pthread), not available on Windows
if(ENABLE_MOCK_SERVER AND NOT WIN32)
  add_executable(c64_mock_server c64_mock_server.c)
  target_link_libraries(c64_mock_server Threads::Threads)

  # Link math library on non-Windows platforms
  target_link_libraries(c64_mock_server m)
elseif(ENABLE_MOCK_SERVER AND WIN32)
  message(STATUS "Mock server disabled on Windows (requires POSIX threads)")
endif()

# Integration test (plugin + mock server) - only build when explicitly enabled
if(ENABLE_INTEGRATION_TESTS)
  add_executable(test_integration test_integration.c)

  # Link against real OBS libraries and other dependencies
  target_link_libraries(test_integration Threads::Threads obs)

  # Link platform-specific libraries
  if(NOT WIN32)
    target_link_libraries(test_integration m dl)
  endif()

  # Include OBS headers
  target_include_directories(test_integration PRIVATE /usr/include/obs)

  # Integration test loads the plugin as a shared library
  add_dependencies(test_integration c64stream)

  add_test(NAME Integration COMMAND test_integration WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Add compiler flags for tests - platform specific
if(NOT IS_CI_BUILD)
  if(MSVC)
    target_compile_options(test_vic_colors PRIVATE /W4 /std:c17)
  else()
    target_compile_options(test_vic_colors PRIVATE -Wall -Wextra -std=c17)
  endif()
endif()

if(ENABLE_MOCK_SERVER AND NOT WIN32)
  if(MSVC)
    target_compile_options(c64_mock_server PRIVATE /W4 /std:c17)
  else()
    target_compile_options(c64_mock_server PRIVATE -Wall -Wextra -std=c17)
  endif()
endif()

if(ENABLE_INTEGRATION_TESTS)
  if(MSVC)
    target_compile_options(test_integration PRIVATE /W4 /std:c17)
  else()
    target_compile_options(test_integration PRIVATE -Wall -Wextra -std=c17)
  endif()
endif()

# Install test binaries to build directory
set(TEST_TARGETS "")
if(NOT IS_CI_BUILD)
  list(APPEND TEST_TARGETS test_vic_colors)
endif()
if(ENABLE_MOCK_SERVER AND NOT WIN32)
  list(APPEND TEST_TARGETS c64_mock_server)
endif()
if(ENABLE_INTEGRATION_TESTS)
  list(APPEND TEST_TARGETS test_integration)
endif()

# Only install targets if we have any to install
if(TEST_TARGETS)
  install(TARGETS ${TEST_TARGETS} RUNTIME DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Add E2E tests subdirectory (Linux only for now)
if(NOT IS_CI_BUILD OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(e2e)
endif()
