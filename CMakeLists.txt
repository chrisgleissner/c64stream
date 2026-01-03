cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Determine authoritative version from git tag first (skip for PR builds)
if(DEFINED ENV{GITHUB_EVENT_NAME} AND "$ENV{GITHUB_EVENT_NAME}" STREQUAL "pull_request")
  # For PR builds, use development version with high patch numbers
  # Use format MAJOR.MINOR.999X where X is build number (CMake VERSION compliant)
  string(REPLACE "." ";" version_parts "${_version}")
  list(GET version_parts 0 major)
  list(GET version_parts 1 minor)

  if(DEFINED ENV{GITHUB_RUN_NUMBER})
    # Use development patch number with limited range to avoid potential issues
    math(EXPR run_mod "$ENV{GITHUB_RUN_NUMBER} % 1000") # Limit to 0-999 range
    math(EXPR dev_patch "900 + ${run_mod}") # Results in 900-1899 range
    set(AUTHORITATIVE_VERSION "${major}.${minor}.${dev_patch}")
  else()
    # Local development builds use 999
    set(AUTHORITATIVE_VERSION "${major}.${minor}.999")
  endif()
  message(STATUS "PR build detected, using development version: ${AUTHORITATIVE_VERSION}")
else()
  execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )

  # Extract clean version number from git tag for project version
  if(GIT_VERSION_TAG AND GIT_VERSION_TAG MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
    # Extract version number from tag (e.g., "v1.2.3" -> "1.2.3", "1.2.3-dirty" -> "1.2.3")
    string(REGEX REPLACE "^v?([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1" AUTHORITATIVE_VERSION "${GIT_VERSION_TAG}")
    message(STATUS "Using git tag version: ${AUTHORITATIVE_VERSION}")
  else()
    # Fallback to buildspec.json version if no proper git tag is available
    set(AUTHORITATIVE_VERSION "${_version}")
    message(STATUS "No git tag found, using buildspec.json version: ${AUTHORITATIVE_VERSION}")
  endif()
endif()

project(${_name} VERSION ${AUTHORITATIVE_VERSION})

# Default to system installation paths for Linux packages unless explicitly overridden
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix for Linux packages" FORCE)
endif()

# Set dummy VirtualCam GUID to prevent Windows build warning
set(VIRTUALCAM_GUID "{00000000-0000-0000-0000-000000000000}")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
option(ENABLE_TESTS "Build tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# Update buildspec.json with authoritative version for consistent packaging
# Always update for PR builds (development versions) and tagged releases
if(AUTHORITATIVE_VERSION)
  # Update buildspec.json with the calculated version to keep it in sync
  # Use a cross-platform approach with temporary file to avoid sed -i incompatibilities
  execute_process(
    COMMAND
      sed "41s/\"version\": \"[^\"]*\"/\"version\": \"${AUTHORITATIVE_VERSION}\"/" "${CMAKE_SOURCE_DIR}/buildspec.json"
    OUTPUT_FILE "${CMAKE_SOURCE_DIR}/buildspec.json.tmp"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE sed_result
  )

  if(sed_result EQUAL 0)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E rename "${CMAKE_SOURCE_DIR}/buildspec.json.tmp" "${CMAKE_SOURCE_DIR}/buildspec.json"
      RESULT_VARIABLE mv_result
    )
    set(sed_result ${mv_result})
  endif()

  if(sed_result EQUAL 0)
    message(STATUS "Updated buildspec.json version to: ${AUTHORITATIVE_VERSION}")
  else()
    message(WARNING "Failed to update buildspec.json version")
  endif()
endif()

# Use git tag for display version (includes dirty/commit info), authoritative version for project
set(DISPLAY_VERSION "${GIT_VERSION_TAG}")

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GIT_HASH)
  set(GIT_HASH "unknown")
endif()

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S" UTC)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE C64_VERSION_TAG="${DISPLAY_VERSION}" C64_GIT_HASH="${GIT_HASH}" C64_BUILD_TIME="${BUILD_TIME}"
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/c64-network.c
    src/c64-network-buffer.c
    src/c64-protocol.c
    src/c64-video.c
    src/c64-color.c
    src/c64-audio.c
    src/c64-source.c
    src/c64-record.c
    src/c64-record-obs.c
    src/c64-record-network.c
    src/c64-record-video.c
    src/c64-record-audio.c
    src/c64-record-frames.c
    src/c64-version.c
    src/c64-properties.c
    src/c64-logo.c
    src/c64-file.c
    src/c64-effect.c
    src/c64-palette.c
    src/c64-rest-client.c
    src/c64-keyboard.c
    src/c64-automation.c
)

# Find and link libcurl for REST API client
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# Link resolver library for DNS functionality on Unix platforms
if(UNIX)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE resolv)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Add tests if enabled
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
  # Ensure the udp_replay tool is available next to the plugin binaries when installing on macOS CI
  # Xcode generator places runtime outputs under per-config directories
  if(APPLE)
    install(
      FILES ${CMAKE_BINARY_DIR}/tests/e2e/$<CONFIG>/udp_replay
      PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
      DESTINATION bin
    )
  endif()
endif()
