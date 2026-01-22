find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
  add_test(
    NAME c64_stream_effects_obs_e2e
    COMMAND ${PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/run_obs_filter_e2e.py
            --output-dir ${CMAKE_CURRENT_BINARY_DIR}/output
            --format NTSC
            --frames 120
  )
  set_tests_properties(c64_stream_effects_obs_e2e PROPERTIES SKIP_RETURN_CODE 77)
else()
  message(WARNING "Python not found, skipping OBS filter E2E test")
endif()
