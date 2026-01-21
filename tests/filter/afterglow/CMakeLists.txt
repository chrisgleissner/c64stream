add_executable(
  test_c64_effect_afterglow
  test_c64_effect_afterglow.c
  ${CMAKE_SOURCE_DIR}/src/c64-effect-afterglow.c
)
target_include_directories(test_c64_effect_afterglow PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(UNIX)
  target_link_libraries(test_c64_effect_afterglow PRIVATE OBS::libobs m)
else()
  target_link_libraries(test_c64_effect_afterglow PRIVATE OBS::libobs)
endif()
add_test(NAME c64_effect_afterglow COMMAND test_c64_effect_afterglow)
