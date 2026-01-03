# C64 Stream Tests
#
# Contains unit tests and end-to-end testing utilities.
#
cmake_minimum_required(VERSION 3.28)

project(c64stream-tests)

# Find libobs for the parser test (needed for logging headers)
find_package(libobs REQUIRED)

# Parser unit tests - compile source files directly
add_executable(
  test_c64script_parser
  test_c64script_parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/c64-script-ast.c
)
target_include_directories(test_c64script_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_c64script_parser PRIVATE OBS::libobs)
add_test(NAME c64script_parser COMMAND test_c64script_parser)

add_subdirectory(e2e)
