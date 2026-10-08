# C64 Stream Tests
#
# Top-level CMake entry for unit tests and test helpers.
cmake_minimum_required(VERSION 3.28)

project(c64stream-tests)

# LibOBS is required by many script validation targets
find_package(libobs REQUIRED)

# C64Script VM contains REST/HTTP helpers that use libcurl.
# Test executables that include c64-script-vm.c must link it as well.
find_package(CURL REQUIRED)

add_subdirectory(script)
add_subdirectory(e2e)
add_subdirectory(filter)
add_subdirectory(network)

# Every locale defines the en-US keys with the same placeholders.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME locale_consistency
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/locale/check_locales.py ${CMAKE_SOURCE_DIR}/data/locale
  )
endif()
