# Explicit opt-in: parent includes this only when wiring ancillary ownership.
function(server1_register_ancillary_tests)
  add_executable(server1_hardware_profiler_tests tests/test_hardware_profiler.cpp)
  target_link_libraries(server1_hardware_profiler_tests PRIVATE colosseum_server_v1)
  add_test(NAME ROOT-profiler-samples-fixtures COMMAND server1_hardware_profiler_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hardware-profiler.json")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME ROOT-profiler-samples-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_hardware_profiler_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/hardware-profiler-oracle.json"
      "$<TARGET_FILE:server1_hardware_profiler_tests>")
  endif()
  if(UNIX)
    add_executable(server1_hardware_probe_isolation_tests tests/test_hardware_probe_isolation.cpp)
    target_link_libraries(server1_hardware_probe_isolation_tests PRIVATE colosseum_server_v1)
    add_test(NAME ROOT-profiler-playback-isolation COMMAND server1_hardware_probe_isolation_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/media-settings-fixture.py")
    set_tests_properties(ROOT-profiler-playback-isolation PROPERTIES TIMEOUT 45)
    find_program(SERVER1_PROFILER_FFMPEG ffmpeg)
    find_program(SERVER1_PROFILER_FFPROBE ffprobe)
    if(SERVER1_PROFILER_FFMPEG AND SERVER1_PROFILER_FFPROBE)
      add_executable(server1_generated_samples_tests tests/test_generated_samples.cpp)
      target_link_libraries(server1_generated_samples_tests PRIVATE colosseum_server_v1)
      add_test(NAME ROOT-generated-samples-live COMMAND server1_generated_samples_tests
        "${SERVER1_PROFILER_FFMPEG}" "${SERVER1_PROFILER_FFPROBE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/server_lab/profiler_samples/manifest.json")
      set_tests_properties(ROOT-generated-samples-live PROPERTIES TIMEOUT 120)
      add_executable(server1_hardware_profiler_real_tests tests/test_hardware_profiler_real.cpp)
      target_link_libraries(server1_hardware_profiler_real_tests PRIVATE colosseum_server_v1)
      add_test(NAME ROOT-profiler-no-assets-live COMMAND server1_hardware_profiler_real_tests
        "${SERVER1_PROFILER_FFMPEG}" "${SERVER1_PROFILER_FFPROBE}")
      set_tests_properties(ROOT-profiler-no-assets-live PROPERTIES TIMEOUT 75)
      if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
        # Assets are extracted from the explicitly supplied verified oracle into
        # a disposable temporary directory; no copied media is in this target.
        add_test(NAME ROOT-profiler-real-loopback COMMAND "${SERVER1_NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_hardware_profiler_oracle.cjs"
          "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/hardware-profiler-real.json"
          "$<TARGET_FILE:server1_hardware_profiler_real_tests>" --real
          "${SERVER1_PROFILER_FFMPEG}" "${SERVER1_PROFILER_FFPROBE}")
        set_tests_properties(ROOT-profiler-real-loopback PROPERTIES TIMEOUT 90)
      endif()
    endif()
  endif()
endfunction()
