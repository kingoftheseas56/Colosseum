set(SERVER1_K10_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k10_tests)
  add_executable(server1_k10_native_transport_test "${SERVER1_K10_ROOT}/tests/test_native_transport.cpp")
  target_link_libraries(server1_k10_native_transport_test PRIVATE server1_k10_native_transport)
  target_compile_features(server1_k10_native_transport_test PRIVATE cxx_std_17)
  target_compile_options(server1_k10_native_transport_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
  set(K10_PACKET_ROOT "${SERVER1_K10_ROOT}/../../artifacts/server1/K10/K10-A")
  add_test(NAME K10-01-native-transport-real-wire
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_wire.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -PortA 49610 -PortB 49611)
  set_tests_properties(K10-01-native-transport-real-wire PROPERTIES LABELS "server1;native;K10")
  foreach(case_id IN ITEMS K10-02 K10-03)
    add_test(NAME ${case_id}-native-transport COMMAND server1_k10_native_transport_test ${case_id} "${CMAKE_CURRENT_BINARY_DIR}/${case_id}")
    set_tests_properties(${case_id}-native-transport PROPERTIES LABELS "server1;native;K10")
  endforeach()
  add_test(NAME K10-02-native-transport-lifecycle
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_lifecycle.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -PortA 49612 -PortB 49613)
  set_tests_properties(K10-02-native-transport-lifecycle PROPERTIES LABELS "server1;native;K10")
endfunction()
