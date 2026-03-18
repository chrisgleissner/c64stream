add_executable(
  test_c64_stream_effects_pipeline
  test_c64_stream_effects_pipeline.c
  obs_stubs.c
  ${CMAKE_SOURCE_DIR}/src/video/c64-stream-effects.c
  ${CMAKE_SOURCE_DIR}/src/video/c64-effect-afterglow.c
)
target_include_directories(test_c64_stream_effects_pipeline PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/util ${CMAKE_SOURCE_DIR}/src/video)
if(UNIX)
  target_link_libraries(test_c64_stream_effects_pipeline PRIVATE OBS::libobs m)
else()
  target_link_libraries(test_c64_stream_effects_pipeline PRIVATE OBS::libobs)
endif()

find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
  add_test(
    NAME c64_stream_effects_pipeline_analysis
    COMMAND
      ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/analyze/run_pipeline_analysis.py --binary
      $<TARGET_FILE:test_c64_stream_effects_pipeline> --output-dir ${CMAKE_CURRENT_BINARY_DIR}/analysis_output
  )
else()
  message(WARNING "Python not found, skipping filter pipeline analysis test")
endif()
