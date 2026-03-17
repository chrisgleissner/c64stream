# C64Script validation suite
# Located in tests/script to keep C64Script-specific helpers together.

# Parser unit tests require libobs headers for logging utilities
add_executable(
  test_c64script_parser
  test_c64script_parser.c
  c64script_test_logging.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
)
target_include_directories(test_c64script_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_c64script_parser PRIVATE OBS::libobs)
add_test(NAME c64script_parser COMMAND test_c64script_parser)

# Compiler and VM unit tests
add_executable(
  test_c64script_compiler
  test_c64script_compiler.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
)
target_include_directories(test_c64script_compiler PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64script_compiler PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_compiler PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64script_compiler COMMAND test_c64script_compiler)

# REST network integration tests (mock server)
add_executable(
  test_c64script_rest_network
  test_c64script_rest_network.c
  c64script_test_logging.c
  c64script_keyboard_stub.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-rest-client.c
)
target_include_directories(test_c64script_rest_network PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_c64script_rest_network PRIVATE C64STREAM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(UNIX)
  target_link_libraries(test_c64script_rest_network PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_rest_network PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64script_rest_network COMMAND test_c64script_rest_network)

# Edge case unit tests
add_executable(
  test_c64script_edge_cases
  test_c64script_edge_cases.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
)
target_include_directories(test_c64script_edge_cases PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64script_edge_cases PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_edge_cases PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64script_edge_cases COMMAND test_c64script_edge_cases)

# Repository-wide script validation - validates ALL .c64script files
add_executable(
  test_c64script_all_scripts
  test_c64script_all_scripts.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
)
target_include_directories(test_c64script_all_scripts PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64script_all_scripts PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_all_scripts PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(
  NAME c64script_all_scripts
  COMMAND test_c64script_all_scripts ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Parser regression tests for every script in data/scripts
file(GLOB SCRIPT_FILES "${CMAKE_SOURCE_DIR}/data/scripts/*.c64script")
foreach(SCRIPT_FILE ${SCRIPT_FILES})
  get_filename_component(SCRIPT_NAME ${SCRIPT_FILE} NAME_WE)

  # Skip demo_sid_playback_cycle until PLAY_SID command is implemented
  if(NOT SCRIPT_NAME STREQUAL "demo_sid_playback_cycle")
    add_test(
      NAME c64script_parse_${SCRIPT_NAME}
      COMMAND test_c64script_parser ${SCRIPT_FILE}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
  endif()
endforeach()

# Python script validation tests
find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
  add_test(
    NAME c64script_validation
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_scripts.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
  message(STATUS "Python script validation tests enabled")
else()
  message(WARNING "Python not found, skipping script validation tests")
endif()

add_subdirectory(fuzz)

# Debug features unit tests
add_executable(
  test_c64script_debug
  test_c64script_debug.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
)
target_include_directories(test_c64script_debug PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64script_debug PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_debug PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64script_debug COMMAND test_c64script_debug)

# Script executor thread-safety test (no OBS UI thread)
add_executable(
  test_c64script_thread_safety
  test_c64script_thread_safety.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-builtins.c
)
target_include_directories(test_c64script_thread_safety PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64script_thread_safety PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64script_thread_safety PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64script_thread_safety COMMAND test_c64script_thread_safety)

# Automation playlist selection test (headless)
add_executable(
  test_automation_playlist
  test_automation_playlist.c
  c64script_test_logging.c
  c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/c64-automation.c
  ${CMAKE_SOURCE_DIR}/src/c64-automation-playlist.c
  ${CMAKE_SOURCE_DIR}/src/c64-automation-hvsc.c
)
target_include_directories(test_automation_playlist PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_automation_playlist PRIVATE OBS::libobs)
add_test(NAME automation_playlist COMMAND test_automation_playlist)

add_executable(test_properties_refresh test_properties_refresh.c ${CMAKE_SOURCE_DIR}/src/c64-properties-refresh.c)
target_include_directories(test_properties_refresh PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_properties_refresh PRIVATE OBS::libobs)
add_test(NAME properties_refresh COMMAND test_properties_refresh)

add_executable(test_playlist_window test_playlist_window.c ${CMAKE_SOURCE_DIR}/src/c64-playlist-window.c)
target_include_directories(test_playlist_window PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME playlist_window COMMAND test_playlist_window)

add_executable(
  test_interact_key
  test_interact_key.c
  c64script_test_logging.c
  ${CMAKE_SOURCE_DIR}/src/c64-interact-key.c
  ${CMAKE_SOURCE_DIR}/src/c64-keyboard.c
)
target_include_directories(test_interact_key PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_interact_key PRIVATE C64STREAM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_interact_key PRIVATE OBS::libobs)
add_test(NAME interact_key COMMAND test_interact_key)
