add_executable(
  test_c64_rest_outcome
  test_c64_rest_outcome.c
  ${CMAKE_SOURCE_DIR}/src/network/c64-rest-client.c
)
target_include_directories(
  test_c64_rest_outcome
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util)
if(UNIX)
  target_link_libraries(test_c64_rest_outcome PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64_rest_outcome PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64_rest_outcome COMMAND test_c64_rest_outcome)

add_executable(test_c64_ingest_filter test_c64_ingest_filter.c)
target_include_directories(
  test_c64_ingest_filter
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util
          ${CMAKE_SOURCE_DIR}/src/audio ${CMAKE_SOURCE_DIR}/src/video ${CMAKE_SOURCE_DIR}/src/record)
if(UNIX)
  target_link_libraries(test_c64_ingest_filter PRIVATE OBS::libobs m)
else()
  target_link_libraries(test_c64_ingest_filter PRIVATE OBS::libobs)
endif()
add_test(NAME c64_ingest_filter COMMAND test_c64_ingest_filter)

add_executable(test_c64_stream_dest test_c64_stream_dest.c)
target_include_directories(test_c64_stream_dest PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network)
if(UNIX)
  target_link_libraries(test_c64_stream_dest PRIVATE m)
endif()
add_test(NAME c64_stream_dest COMMAND test_c64_stream_dest)

add_executable(test_c64_device test_c64_device.c ${CMAKE_SOURCE_DIR}/src/device/c64-device.c
               ${CMAKE_SOURCE_DIR}/src/util/c64-file.c)
target_include_directories(test_c64_device PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/device
                           ${CMAKE_SOURCE_DIR}/src/util)
target_link_libraries(test_c64_device PRIVATE OBS::libobs)
add_test(NAME c64_device COMMAND test_c64_device)
