option(C64STREAM_ENABLE_FUZZING "Enable C64Script fuzz targets" OFF)

if(NOT C64STREAM_ENABLE_FUZZING)
  return()
endif()

if(NOT LIBFUZZER_LIBRARY)
  find_library(
    LIBFUZZER_LIBRARY
    NAMES Fuzzer libFuzzer
    PATHS /usr/lib/llvm-14/lib /usr/lib/llvm-15/lib /usr/lib/llvm-17/lib /usr/lib/llvm-18/lib
  )
endif()
if(NOT LIBFUZZER_LIBRARY)
  message(FATAL_ERROR "libFuzzer library not found. Install libfuzzer-14-dev or adjust LIBFUZZER_LIBRARY.")
endif()

add_executable(
  c64script_fuzz
  fuzz_c64script.c
  ${CMAKE_SOURCE_DIR}/tests/script/c64script_test_logging.c
  ${CMAKE_SOURCE_DIR}/tests/script/c64script_test_stubs.c
  ${CMAKE_SOURCE_DIR}/src/script/frontend/c64-script-parser.c
  ${CMAKE_SOURCE_DIR}/src/script/frontend/c64-script-token.c
  ${CMAKE_SOURCE_DIR}/src/script/frontend/c64-script-ast.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-bytecode.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-core.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-builtins.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-config.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-drives.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-effects.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-io.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-keyboard.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-machine.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-dispatch-memory.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-trace.c
  ${CMAKE_SOURCE_DIR}/src/script/vm/c64-script-vm-utils.c
  ${CMAKE_SOURCE_DIR}/src/script/runtime/c64-script-runtime.c
  ${CMAKE_SOURCE_DIR}/src/script/runtime/c64-script-builtins.c
)

target_include_directories(c64script_fuzz PRIVATE ${C64_ALL_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/tests/script)

target_compile_definitions(c64script_fuzz PRIVATE C64SCRIPT_FUZZING=1)

set(FUZZ_SANITIZERS "-fsanitize=fuzzer-no-link,address,undefined")
set(FUZZ_COVERAGE "-fsanitize-coverage=inline-8bit-counters,trace-cmp,pc-table")

target_compile_options(
  c64script_fuzz
  PRIVATE ${FUZZ_SANITIZERS} ${FUZZ_COVERAGE} -fno-omit-frame-pointer -g -O1
)
target_link_options(c64script_fuzz PRIVATE ${FUZZ_SANITIZERS})

if(UNIX)
  target_link_libraries(
    c64script_fuzz
    PRIVATE OBS::libobs CURL::libcurl m ${LIBFUZZER_LIBRARY} stdc++
  )
else()
  target_link_libraries(c64script_fuzz PRIVATE OBS::libobs CURL::libcurl ${LIBFUZZER_LIBRARY})
endif()

set_target_properties(c64script_fuzz PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/script/fuzz")
