# Optional NZB byte sources and direct-video HTTP routes; mountNzbRoutes is explicit.
find_package(ZLIB REQUIRED)
target_sources(colosseum_server_v1 PRIVATE src/nzb/NzbSource.cpp src/http/NzbRoutes.cpp)
target_link_libraries(colosseum_server_v1 PRIVATE ZLIB::ZLIB)
if(BUILD_TESTING)
  add_executable(server1_nzb_source_tests tests/test_nzb_source.cpp)
  target_link_libraries(server1_nzb_source_tests PRIVATE colosseum_server_v1)
  add_executable(server1_nzb_routes_tests tests/test_nzb_routes.cpp)
  target_link_libraries(server1_nzb_routes_tests PRIVATE colosseum_server_v1)
  add_executable(server1_nzb_remote_range_tests tests/test_nzb_remote_range.cpp)
  target_link_libraries(server1_nzb_remote_range_tests PRIVATE colosseum_server_v1)
  add_test(NAME N09-nzb-remote-range COMMAND server1_nzb_remote_range_tests)
  set_tests_properties(N09-nzb-remote-range PROPERTIES TIMEOUT 15)
  add_test(NAME N09-nzb-http-loopback COMMAND server1_nzb_routes_tests)
  set_tests_properties(N09-nzb-http-loopback PROPERTIES TIMEOUT 30)
  add_test(NAME N09-nzb-loopback COMMAND server1_nzb_source_tests)
  set_tests_properties(N09-nzb-loopback PROPERTIES TIMEOUT 15)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME N09-nzb-http-oracle-generate COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nzb_routes_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/nzb-routes-oracle.json")
    set_tests_properties(N09-nzb-http-oracle-generate PROPERTIES FIXTURES_SETUP nzb_http_oracle TIMEOUT 30)
    add_test(NAME N09-nzb-http-differential COMMAND server1_nzb_routes_tests
      "${CMAKE_CURRENT_BINARY_DIR}/nzb-routes-oracle.json" "${SERVER1_K12_ORACLE}" "${SERVER1_NODE_EXECUTABLE}")
    set_tests_properties(N09-nzb-http-differential PROPERTIES FIXTURES_REQUIRED nzb_http_oracle TIMEOUT 45)
    add_test(NAME N09-nzb-oracle-generate COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_nzb_oracle.cjs"
      "${SERVER1_K12_ORACLE}" "${CMAKE_CURRENT_BINARY_DIR}/nzb-oracle.json")
    set_tests_properties(N09-nzb-oracle-generate PROPERTIES FIXTURES_SETUP nzb_oracle TIMEOUT 30)
    add_test(NAME N09-nzb-differential COMMAND server1_nzb_source_tests "${CMAKE_CURRENT_BINARY_DIR}/nzb-oracle.json")
    set_tests_properties(N09-nzb-differential PROPERTIES FIXTURES_REQUIRED nzb_oracle TIMEOUT 15)
  endif()
endif()
