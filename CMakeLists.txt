cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Keep configure output clean (OBS subprojects can emit noisy developer warnings).
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE BOOL "Suppress CMake developer warnings" FORCE)

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

# Set dummy VirtualCam GUID (must be a CACHE var for OBS subprojects).
# Note: OBS' GUID validator expects a plain UUID without braces.
set(VIRTUALCAM_GUID "00000000-0000-0000-0000-000000000000" CACHE STRING "Virtual camera GUID" FORCE)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
option(ENABLE_TESTS "Build tests" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# Build bundled C64 PRGs (e.g. A/V sync auto-trigger)
set(C64_64TASS_HINTS "")
if(OS_WINDOWS)
  # Common MSYS2 locations (recommended upstream install path for Windows).
  list(APPEND C64_64TASS_HINTS "C:/msys64/usr/bin" "C:/msys64/mingw64/bin")
endif()
find_program(C64_64TASS_EXECUTABLE NAMES 64tass 64tass.exe HINTS ${C64_64TASS_HINTS})
if(C64_64TASS_EXECUTABLE)
  file(GLOB C64_C64_ASM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tools/c64/*.asm")
  set(C64_PRG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/prg")

  set(C64_C64_PRG_OUTPUTS "")
  foreach(c64_asm IN LISTS C64_C64_ASM_FILES)
    cmake_path(GET c64_asm STEM c64_stem)
    set(c64_prg "${C64_PRG_OUTPUT_DIR}/${c64_stem}.prg")

    add_custom_command(
      OUTPUT "${c64_prg}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${C64_PRG_OUTPUT_DIR}"
      COMMAND "${C64_64TASS_EXECUTABLE}" --cbm-prg --labels --quiet -o "${c64_prg}" "${c64_asm}"
      DEPENDS "${c64_asm}"
      VERBATIM
    )

    list(APPEND C64_C64_PRG_OUTPUTS "${c64_prg}")
  endforeach()

  add_custom_target(c64_prgs ALL DEPENDS ${C64_C64_PRG_OUTPUTS})

  # Ensure the PRGs are available in dev rundir and shipped in install artifacts.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${C64_C64_PRG_OUTPUTS})
  if(OS_MACOS)
    foreach(c64_prg IN LISTS C64_C64_PRG_OUTPUTS)
      set_property(SOURCE "${c64_prg}" PROPERTY MACOSX_PACKAGE_LOCATION "Resources/prg")
    endforeach()
  endif()

  if(OS_WINDOWS)
    install(FILES ${C64_C64_PRG_OUTPUTS} DESTINATION "${CMAKE_PROJECT_NAME}/data/prg")
  elseif(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
    install(
      FILES ${C64_C64_PRG_OUTPUTS}
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}/prg"
    )
  endif()

  if(NOT OS_MACOS)
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND
        "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/prg"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different ${C64_C64_PRG_OUTPUTS}
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/prg"
      COMMENT "Copy generated PRGs to rundir"
      VERBATIM
    )
  endif()
else()
  message(STATUS "64tass not found; using prebuilt PRGs from data/prg")

  # Fall back to prebuilt PRGs tracked in the repository (important for Windows CI packaging).
  file(GLOB C64_PREBUILT_PRGS "${CMAKE_CURRENT_SOURCE_DIR}/data/prg/*.prg")
  if(C64_PREBUILT_PRGS)
    if(OS_WINDOWS)
      install(FILES ${C64_PREBUILT_PRGS} DESTINATION "${CMAKE_PROJECT_NAME}/data/prg")
    elseif(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
      install(
        FILES ${C64_PREBUILT_PRGS}
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}/prg"
      )
    endif()
  else()
    message(STATUS "No prebuilt PRGs found under data/prg; PRG folder in packages may be empty")
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

find_package(Python3 COMPONENTS Interpreter)
find_package(libobs REQUIRED)
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# PNG read/write for ASSERT IMAGE_EQUALS is provided by the vendored stb_image
# and stb_image_write headers in src/video/. We deliberately do NOT link
# libpng/png.framework here — on macOS the obs-deps prebuilt ships libpng as
# png.framework, which gets baked into the plugin's install names but is not
# present in the OBS app bundle end users install, breaking plugin loading
# (issue #116).

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_FRONTEND_API)
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
    src/c64-source.c
    src/device/c64-device.c
    src/device/c64-device-scan.c
    src/util/c64-av-sync.c
    src/util/c64-dimensions.c
    src/util/c64-file.c
    src/util/c64-version.c
    src/audio/c64-audio.c
    src/network/c64-rest-client.c
    src/network/c64-network.c
    src/network/c64-network-buffer.c
    src/network/c64-protocol.c
    src/network/c64-stream-control.c
    src/video/c64-video.c
    src/video/c64-effect-afterglow.c
    src/video/c64-effect-geometry.c
    src/video/c64-color.c
    src/video/c64-stream-effects.c
    src/video/c64-logo.c
    src/video/c64-effect.c
    src/video/c64-palette.c
    src/record/c64-record.c
    src/record/c64-record-obs.c
    src/record/c64-record-network.c
    src/record/c64-record-video.c
    src/record/c64-record-audio.c
    src/record/c64-record-frames.c
    src/ui/c64-interact-key.c
    src/ui/c64-properties.c
    src/ui/c64-properties-refresh.c
    src/ui/c64-playlist-window.c
    src/ui/c64-keyboard.c
    src/automation/c64-automation.c
    src/automation/c64-automation-playlist.c
    src/automation/c64-automation-hvsc.c
    src/script/frontend/c64-script-token.c
    src/script/frontend/c64-script-ast.c
    src/script/frontend/c64-script-parser.c
    src/script/vm/c64-script-bytecode.c
    src/script/vm/c64-script-vm.c
    src/script/vm/c64-script-vm-dispatch.c
    src/script/vm/c64-script-vm-dispatch-core.c
    src/script/vm/c64-script-vm-dispatch-builtins.c
    src/script/vm/c64-script-vm-dispatch-config.c
    src/script/vm/c64-script-vm-dispatch-drives.c
    src/script/vm/c64-script-vm-dispatch-effects.c
    src/script/vm/c64-script-vm-dispatch-io.c
    src/script/vm/c64-script-vm-dispatch-keyboard.c
    src/script/vm/c64-script-vm-dispatch-machine.c
    src/script/vm/c64-script-vm-dispatch-memory.c
    src/script/vm/c64-script-vm-trace.c
    src/script/vm/c64-script-vm-utils.c
    src/script/runtime/c64-script-executor.c
    src/script/runtime/c64-script-runtime.c
    src/script/runtime/c64-script-builtins.c
)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src
    src/audio
    src/automation
    src/network
    src/record
    src/script
    src/script/frontend
    src/script/vm
    src/script/runtime
    src/ui
    src/util
    src/video
)

# Find and link libcurl for REST API client
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# Link resolver library for DNS functionality on Unix platforms
if(UNIX)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE resolv)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
target_add_resource(${CMAKE_PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

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
