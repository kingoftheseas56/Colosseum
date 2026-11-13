function(server1_register_m07_tests)
  add_executable(server1_m07_legacy_hls_tests tests/test_legacy_hls.cpp)
  target_link_libraries(server1_m07_legacy_hls_tests PRIVATE colosseum_server_v1)
  add_executable(server1_legacy_media_index_test tests/test_legacy_media_index.cpp)
  target_link_libraries(server1_legacy_media_index_test PRIVATE colosseum_server_v1)
  add_executable(server1_legacy_render_test tests/test_legacy_render.cpp)
  target_link_libraries(server1_legacy_render_test PRIVATE colosseum_server_v1)
  add_test(NAME M07-render-fixtures COMMAND server1_legacy_render_test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy-render-parity.json")
  add_test(NAME M07-legacy-media-index COMMAND server1_legacy_media_index_test)
  set_tests_properties(M07-legacy-media-index PROPERTIES TIMEOUT 60)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME M07-render-differential
      COMMAND "${SERVER1_NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_render_oracle.cjs"
        "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/m07-render-oracle.json"
        "$<TARGET_FILE:server1_legacy_render_test>")
    set_tests_properties(M07-render-differential PROPERTIES TIMEOUT 45)
  endif()
  if(UNIX)
    # The process fixture is a Python shebang and this lane uses real local media.
    add_test(NAME M07-factory-lifecycle-routes-media
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_media.py"
        "$<TARGET_FILE:server1_m07_legacy_hls_tests>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy-factory.json"
        "${CMAKE_CURRENT_BINARY_DIR}/m07-real-media")
    set_tests_properties(M07-factory-lifecycle-routes-media PROPERTIES TIMEOUT 90)
    add_executable(server1_legacy_runtime_tests tests/test_legacy_runtime.cpp)
    target_link_libraries(server1_legacy_runtime_tests PRIVATE colosseum_server_v1)
    add_test(NAME M07-runtime-routes-lifecycle COMMAND server1_legacy_runtime_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy-runtime-fixture.py" --timeout)
    set_tests_properties(M07-runtime-routes-lifecycle PROPERTIES TIMEOUT 45)
    add_test(NAME M07-production-composition
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_composition.py"
        --host "$<TARGET_FILE:server1_host>"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy-runtime-fixture.py"
        --work "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(M07-production-composition PROPERTIES TIMEOUT 45)
    if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
      add_test(NAME M07-probe-runtime-oracle
        COMMAND "${SERVER1_NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_runtime_oracle.cjs"
          "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy-runtime-fixture.py"
          "${CMAKE_CURRENT_BINARY_DIR}/m07-runtime-oracle.json")
      set_tests_properties(M07-probe-runtime-oracle PROPERTIES TIMEOUT 45)
      add_test(NAME M07-media-index-differential
        COMMAND "${SERVER1_NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy-media-index-oracle.cjs"
          "${SERVER1_K12_ORACLE}" "$<TARGET_FILE:server1_legacy_media_index_test>"
          "${CMAKE_CURRENT_BINARY_DIR}/m07-index-oracle")
      set_tests_properties(M07-media-index-differential PROPERTIES TIMEOUT 120)
    endif()
  endif()
endfunction()
