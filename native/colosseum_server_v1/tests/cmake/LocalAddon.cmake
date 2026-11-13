# Include after colosseum_server_v1 is defined, then call the registration function.
target_sources(colosseum_server_v1 PRIVATE src/local/LocalAddon.cpp src/local/LocalMetadata.cpp)
function(server1_register_local_addon_tests)
  add_executable(server1_local_addon_tests tests/test_local_addon.cpp)
  target_link_libraries(server1_local_addon_tests PRIVATE colosseum_server_v1)
  add_test(NAME LOCAL-lifecycle COMMAND server1_local_addon_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/local-addon.json")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME LOCAL-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_local_addon_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/local-addon-oracle.json"
      "$<TARGET_FILE:server1_local_addon_tests>")
    set_tests_properties(LOCAL-differential PROPERTIES TIMEOUT 45)
  endif()
endfunction()
