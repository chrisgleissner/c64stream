# C64 Stream End-to-End Tests
#
# This directory contains automated E2E tests that verify the plugin
# with simulated C64 Ultimate streams following the exact protocol specification.
#
cmake_minimum_required(VERSION 3.28)

project(c64stream-e2e-tests)

# Build the UDP replay tool
add_executable(udp_replay udp_replay.c)

# Compiler flags for the UDP replay tool
if(MSVC)
  target_compile_options(udp_replay PRIVATE /W4 /std:c17)
  # Link with Winsock on Windows
  target_link_libraries(udp_replay PRIVATE ws2_32)
else()
  target_compile_options(udp_replay PRIVATE -Wall -Wextra -std=c17 -O2)
endif()

# Make the Python scripts executable
file(GLOB PYTHON_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
foreach(script ${PYTHON_SCRIPTS})
  file(
    CHMOD
    ${script}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )
endforeach()

# E2E test files are not installed to the package - they are only used during local development and CI
# The udp_replay tool and Python scripts remain in the build tree for testing purposes

message(STATUS "E2E test components configured:")
message(STATUS "  UDP replay tool: udp_replay (build-tree only)")
message(STATUS "  Packet generator: util/generate_packets.py (build-tree only)")
message(STATUS "  Test orchestrator: e2e.py (build-tree only)")
message(STATUS "  Output verifier: verify_output.py (build-tree only)")
