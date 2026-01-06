# C64 Stream Tests
#
# Top-level CMake entry for unit tests and test helpers.
cmake_minimum_required(VERSION 3.28)

project(c64stream-tests)

# LibOBS is required by many script validation targets
find_package(libobs REQUIRED)

add_subdirectory(script)
add_subdirectory(e2e)
