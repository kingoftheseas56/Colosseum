if(BUILD_TESTING)
  add_executable(server1_embedded_stream_test
    tests/test_embedded_stream_server.cpp
    ../player/streamserver.cpp ../player/streamserver.h)
  set_target_properties(server1_embedded_stream_test PROPERTIES AUTOMOC ON)
  target_compile_definitions(server1_embedded_stream_test PRIVATE COLOSSEUM_ENABLE_SERVER1=1)
  target_include_directories(server1_embedded_stream_test PRIVATE ../player)
  target_link_libraries(server1_embedded_stream_test PRIVATE colosseum_server_v1 Qt6::Core Qt6::Network)
  add_executable(server1_stream_settings_gate_test
    tests/test_stream_settings_gate.cpp ../player/streamserver.cpp ../player/streamserver.h)
  set_target_properties(server1_stream_settings_gate_test PROPERTIES AUTOMOC ON)
  target_compile_definitions(server1_stream_settings_gate_test PRIVATE COLOSSEUM_ENABLE_SERVER1=1)
  target_include_directories(server1_stream_settings_gate_test PRIVATE ../player)
  target_link_libraries(server1_stream_settings_gate_test PRIVATE colosseum_server_v1 Qt6::Core Qt6::Network)
  add_test(NAME E00-stream-settings-gate COMMAND server1_stream_settings_gate_test)
  set_tests_properties(E00-stream-settings-gate PROPERTIES TIMEOUT 15 LABELS "server1;embedded;settings")
  if(UNIX)
    add_test(NAME E00-embedded-stream-server COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_embedded_stream_server.py"
      --candidate "$<TARGET_FILE:server1_embedded_stream_test>"
      --media "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/M01/media/tiny.mp4"
      --work "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(E00-embedded-stream-server PROPERTIES TIMEOUT 45 LABELS "server1;embedded;media")
  endif()
endif()
