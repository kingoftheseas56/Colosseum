# Include independently after LocalAddon.cmake. Existing files remain unchanged.
target_sources(colosseum_server_v1 PRIVATE src/local/LocalFilename.cpp
  src/local/LocalImdb.cpp src/local/LocalIndexer.cpp src/local/LocalAddonLog.cpp src/local/LocalAddonRuntime.cpp)
function(server1_register_local_indexing_tests)
  add_executable(server1_local_filename_tests tests/test_local_filename.cpp)
  target_link_libraries(server1_local_filename_tests PRIVATE colosseum_server_v1)
  add_test(NAME LOCAL-filename-fixtures COMMAND server1_local_filename_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/local-filename.json")
  add_executable(server1_local_indexing_tests tests/test_local_indexing.cpp)
  target_link_libraries(server1_local_indexing_tests PRIVATE colosseum_server_v1)
  add_test(NAME LOCAL-indexing-fixtures COMMAND server1_local_indexing_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/local-indexing.json")
  add_test(NAME LOCAL-startup-catalog-setting COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local_startup_setting.py"
    --host "$<TARGET_FILE:server1_host>" --work "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(LOCAL-startup-catalog-setting PROPERTIES TIMEOUT 60)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    foreach(component filename indexing)
      add_test(NAME LOCAL-${component}-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local_${component}_oracle.cjs"
        "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/local-${component}.json"
        "$<TARGET_FILE:server1_local_${component}_tests>")
      set_tests_properties(LOCAL-${component}-differential PROPERTIES TIMEOUT 60)
    endforeach()
  endif()
endfunction()
