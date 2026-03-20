add_executable(
  test_c64_effect_geometry
  test_c64_effect_geometry.c
  ${CMAKE_SOURCE_DIR}/src/video/c64-effect-geometry.c
)
target_include_directories(test_c64_effect_geometry PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/video)
if(UNIX)
  target_link_libraries(test_c64_effect_geometry PRIVATE OBS::libobs m)
else()
  target_link_libraries(test_c64_effect_geometry PRIVATE OBS::libobs)
endif()
add_test(NAME c64_effect_geometry COMMAND test_c64_effect_geometry)
