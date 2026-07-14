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
