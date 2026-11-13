# Include after colosseum_server_v1 exists, then call both functions once.
function(server1_add_youtube_sources)
  target_sources(colosseum_server_v1 PRIVATE src/youtube/YouTubeResolver.cpp src/http/YouTubeRoutes.cpp)
endfunction()
function(server1_register_youtube_tests)
  add_executable(server1_youtube_lifetime_tests tests/test_youtube_lifetime.cpp)
  target_include_directories(server1_youtube_lifetime_tests PRIVATE include)
  target_link_libraries(server1_youtube_lifetime_tests PRIVATE Qt6::Core Qt6::Network)
  add_test(NAME YT-network-callback-lifetime COMMAND server1_youtube_lifetime_tests)
  set_tests_properties(YT-network-callback-lifetime PROPERTIES TIMEOUT 10)
  add_executable(server1_youtube_tests tests/test_youtube.cpp)
  target_link_libraries(server1_youtube_tests PRIVATE colosseum_server_v1)
  add_test(NAME YT-fetched-page-fixtures COMMAND server1_youtube_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/youtube.json")
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME YT-pinned-resolver-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_youtube_oracle.cjs" "${SERVER1_K12_ORACLE}"
      "${CMAKE_CURRENT_BINARY_DIR}/youtube-oracle.json" "$<TARGET_FILE:server1_youtube_tests>")
  endif()
endfunction()
