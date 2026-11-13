function(server1_register_root_tests)
  add_executable(server1_root_routes_tests tests/test_root_routes.cpp)
  target_link_libraries(server1_root_routes_tests PRIVATE colosseum_server_v1)
  add_test(NAME ROOT-routes-fixtures COMMAND server1_root_routes_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/root-routes.json")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME ROOT-routes-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_root_routes_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/root-routes-oracle.json"
      "$<TARGET_FILE:server1_root_routes_tests>")
    set_tests_properties(ROOT-routes-differential PROPERTIES TIMEOUT 45)
  endif()
  if(UNIX)
    add_executable(server1_media_settings_tests tests/test_media_settings.cpp)
    target_link_libraries(server1_media_settings_tests PRIVATE colosseum_server_v1)
    add_test(NAME ROOT-settings-live-media COMMAND server1_media_settings_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/media-settings-fixture.py"
      "${CMAKE_CURRENT_BINARY_DIR}/media-settings-result.json")
    set_tests_properties(ROOT-settings-live-media PROPERTIES TIMEOUT 45)
    if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
      add_test(NAME ROOT-settings-media-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_media_settings_oracle.cjs"
        "${SERVER1_K12_ORACLE}" "$<TARGET_FILE:server1_media_settings_tests>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/media-settings-fixture.py"
        "${CMAKE_CURRENT_BINARY_DIR}/media-settings-oracle.json")
      set_tests_properties(ROOT-settings-media-differential PROPERTIES TIMEOUT 45)
    endif()
  endif()
endfunction()
