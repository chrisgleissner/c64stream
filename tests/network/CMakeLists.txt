add_executable(test_c64_rest_outcome test_c64_rest_outcome.c ${CMAKE_SOURCE_DIR}/src/network/c64-rest-client.c)
target_include_directories(
  test_c64_rest_outcome
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util
)
if(UNIX)
  target_link_libraries(test_c64_rest_outcome PRIVATE OBS::libobs CURL::libcurl m)
else()
  target_link_libraries(test_c64_rest_outcome PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64_rest_outcome COMMAND test_c64_rest_outcome)

add_executable(test_c64_ingest_filter test_c64_ingest_filter.c)
target_include_directories(
  test_c64_ingest_filter
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/video
    ${CMAKE_SOURCE_DIR}/src/record
)
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

add_executable(test_c64_network_buffer test_c64_network_buffer.c ${CMAKE_SOURCE_DIR}/src/network/c64-network-buffer.c)
target_include_directories(
  test_c64_network_buffer
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util
)
if(UNIX)
  target_link_libraries(test_c64_network_buffer PRIVATE OBS::libobs m pthread)
else()
  target_link_libraries(test_c64_network_buffer PRIVATE OBS::libobs)
endif()
add_test(NAME c64_network_buffer COMMAND test_c64_network_buffer)

add_executable(
  test_c64_avi_segments test_c64_avi_segments.c ${CMAKE_SOURCE_DIR}/src/record/c64-record-video.c
)
target_include_directories(
  test_c64_avi_segments
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/record ${CMAKE_SOURCE_DIR}/src/util
          ${CMAKE_SOURCE_DIR}/src/video ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/audio
)
target_link_libraries(test_c64_avi_segments PRIVATE OBS::libobs)
add_test(NAME c64_avi_segments COMMAND test_c64_avi_segments)

# C64STR-014: per-source palette / colour-LUT isolation. Links only the
# dependency-free colour module.
add_executable(
  test_c64_palette_isolation test_c64_palette_isolation.c ${CMAKE_SOURCE_DIR}/src/video/c64-color.c
)
target_include_directories(
  test_c64_palette_isolation PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/video
)
if(UNIX)
  target_link_libraries(test_c64_palette_isolation PRIVATE pthread)
endif()
add_test(NAME c64_palette_isolation COMMAND test_c64_palette_isolation)

# C64STR-017: in-place REST retarget keeps a running script's client valid.
add_executable(
  test_rest_client_retarget test_rest_client_retarget.c ${CMAKE_SOURCE_DIR}/src/network/c64-rest-client.c
)
target_include_directories(
  test_rest_client_retarget
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util
)
if(UNIX)
  target_link_libraries(test_rest_client_retarget PRIVATE OBS::libobs CURL::libcurl m pthread)
else()
  target_link_libraries(test_rest_client_retarget PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME rest_client_retarget COMMAND test_rest_client_retarget)

# C64STR-002: a password change is carried by the next REST request.
add_executable(
  test_rest_password_change test_rest_password_change.c ${CMAKE_SOURCE_DIR}/src/network/c64-rest-client.c
)
target_include_directories(
  test_rest_password_change
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/util
)
if(UNIX)
  target_link_libraries(test_rest_password_change PRIVATE OBS::libobs CURL::libcurl m pthread)
else()
  target_link_libraries(test_rest_password_change PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME rest_password_change COMMAND test_rest_password_change)

# C64STR-008: coherent publication of frame dimensions vs the render thread.
add_executable(
  test_frame_dimensions test_frame_dimensions.c ${CMAKE_SOURCE_DIR}/src/util/c64-dimensions.c
)
target_include_directories(
  test_frame_dimensions PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/util
)
if(UNIX)
  target_link_libraries(test_frame_dimensions PRIVATE pthread)
endif()
add_test(NAME frame_dimensions COMMAND test_frame_dimensions)

# C64STR-021 (+C64STR-023): packet-derived frame height is clamped to the buffer size.
add_executable(test_frame_height_clamp test_frame_height_clamp.c)
target_include_directories(
  test_frame_height_clamp PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network
)
add_test(NAME frame_height_clamp COMMAND test_frame_height_clamp)

# C64STR-010/011: WAV header carries the true rate and clamps past 4 GiB.
add_executable(
  test_wav_header test_wav_header.c ${CMAKE_SOURCE_DIR}/src/record/c64-record-audio.c
)
target_include_directories(
  test_wav_header PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/record
                          ${CMAKE_SOURCE_DIR}/src/util ${CMAKE_SOURCE_DIR}/src/audio
                          ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/video
)
target_link_libraries(test_wav_header PRIVATE OBS::libobs)
add_test(NAME wav_header COMMAND test_wav_header)

# C64STR-028/029: effect parameter clamping and NaN/inf rejection.
add_executable(test_effect_clamp test_effect_clamp.c)
target_include_directories(test_effect_clamp PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/util)
if(UNIX)
  target_link_libraries(test_effect_clamp PRIVATE m)
endif()
add_test(NAME effect_clamp COMMAND test_effect_clamp)

# C64STR-016: each source instance gets a distinct default UDP port pair.
add_executable(test_default_ports test_default_ports.c)
target_include_directories(test_default_ports PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/network)
add_test(NAME default_ports COMMAND test_default_ports)

# C64STR-004: native recursive directory creation (no system("mkdir")).
add_executable(test_file_utils test_file_utils.c ${CMAKE_SOURCE_DIR}/src/util/c64-file.c)
target_include_directories(
  test_file_utils PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/util
)
target_link_libraries(test_file_utils PRIVATE OBS::libobs)
add_test(NAME file_utils COMMAND test_file_utils)

# C64STR-018/024: CSV recording start/stop vs writes are serialised (no UAF).
add_executable(
  test_record_csv_concurrency test_record_csv_concurrency.c
  ${CMAKE_SOURCE_DIR}/src/record/c64-record.c ${CMAKE_SOURCE_DIR}/src/record/c64-record-obs.c
  ${CMAKE_SOURCE_DIR}/src/record/c64-record-network.c
)
target_include_directories(
  test_record_csv_concurrency
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/record ${CMAKE_SOURCE_DIR}/src/util
          ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/audio ${CMAKE_SOURCE_DIR}/src/video
)
if(UNIX)
  target_link_libraries(test_record_csv_concurrency PRIVATE OBS::libobs pthread m)
else()
  target_link_libraries(test_record_csv_concurrency PRIVATE OBS::libobs)
endif()
add_test(NAME record_csv_concurrency COMMAND test_record_csv_concurrency)

# C64STR-009: recording start reports failure (no false success) on open error.
add_executable(
  test_recording_open_failure test_recording_open_failure.c
  ${CMAKE_SOURCE_DIR}/src/record/c64-record.c ${CMAKE_SOURCE_DIR}/src/record/c64-record-obs.c
  ${CMAKE_SOURCE_DIR}/src/record/c64-record-network.c
)
target_include_directories(
  test_recording_open_failure
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/record ${CMAKE_SOURCE_DIR}/src/util
          ${CMAKE_SOURCE_DIR}/src/network ${CMAKE_SOURCE_DIR}/src/audio ${CMAKE_SOURCE_DIR}/src/video
)
if(UNIX)
  target_link_libraries(test_recording_open_failure PRIVATE OBS::libobs pthread m)
else()
  target_link_libraries(test_recording_open_failure PRIVATE OBS::libobs)
endif()
add_test(NAME recording_open_failure COMMAND test_recording_open_failure)

add_executable(test_c64_stream_control test_c64_stream_control.c ${CMAKE_SOURCE_DIR}/src/network/c64-stream-control.c)
target_include_directories(
  test_c64_stream_control
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/video
)
target_link_libraries(test_c64_stream_control PRIVATE OBS::libobs)
add_test(NAME c64_stream_control COMMAND test_c64_stream_control)

add_executable(
  test_c64_device
  test_c64_device.c
  ${CMAKE_SOURCE_DIR}/src/device/c64-device.c
  ${CMAKE_SOURCE_DIR}/src/util/c64-file.c
)
target_include_directories(
  test_c64_device
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/device ${CMAKE_SOURCE_DIR}/src/util
)
target_link_libraries(test_c64_device PRIVATE OBS::libobs)
add_test(NAME c64_device COMMAND test_c64_device)

add_executable(
  test_c64_device_scan
  test_c64_device_scan.c
  ${CMAKE_SOURCE_DIR}/src/device/c64-device-scan.c
  ${CMAKE_SOURCE_DIR}/src/network/c64-network.c
  ${CMAKE_SOURCE_DIR}/src/device/c64-device.c
  ${CMAKE_SOURCE_DIR}/src/util/c64-file.c
)
target_include_directories(
  test_c64_device_scan
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/device
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/src/video
)
if(UNIX)
  target_link_libraries(test_c64_device_scan PRIVATE OBS::libobs CURL::libcurl m resolv)
else()
  target_link_libraries(test_c64_device_scan PRIVATE OBS::libobs CURL::libcurl)
endif()
add_test(NAME c64_device_scan COMMAND test_c64_device_scan)
