# C64 Stream Acceptance Tests
#
# This directory contains automated acceptance tests that verify the plugin
# with simulated C64 Ultimate streams following the exact protocol specification.
#
cmake_minimum_required(VERSION 3.28)

project(c64stream-acceptance-tests)

# Build the UDP replay tool
add_executable(udp_replay udp_replay.c)

# Compiler flags for the UDP replay tool
if(MSVC)
  target_compile_options(udp_replay PRIVATE /W4 /std:c17)
else()
  target_compile_options(udp_replay PRIVATE -Wall -Wextra -std=c17 -O2)
endif()

# Make the Python scripts executable
file(GLOB PYTHON_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
foreach(script ${PYTHON_SCRIPTS})
  file(CHMOD ${script} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()

# Install acceptance test files
install(TARGETS udp_replay RUNTIME DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
install(FILES 
  generate_packets.py
  run_acceptance_test.py
  verify_output.py
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

message(STATUS "Acceptance test components configured:")
message(STATUS "  UDP replay tool: udp_replay")
message(STATUS "  Packet generator: generate_packets.py")
message(STATUS "  Test orchestrator: run_acceptance_test.py")
message(STATUS "  Output verifier: verify_output.py")
