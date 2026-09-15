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
  add_test(NAME K10-01-native-transport-submit-after-ready
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_submit_ready.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49810)
  set_tests_properties(K10-01-native-transport-submit-after-ready PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-sequential-wire
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_sequential.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49811)
  set_tests_properties(K10-02-native-transport-sequential-wire PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-have-wire
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_have.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49812)
  set_tests_properties(K10-02-native-transport-have-wire PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-live-reuse
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_reuse.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49813)
  set_tests_properties(K10-02-native-transport-live-reuse PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-failure-drain
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_failure_drain.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -FailingPort 49815 -SurvivingPort 49816)
  set_tests_properties(K10-02-native-transport-failure-drain PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-cancel-drain
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_cancel_drain.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49817)
  set_tests_properties(K10-02-native-transport-cancel-drain PROPERTIES LABELS "server1;native;K10")
  foreach(case_id IN ITEMS K10-02)
    add_test(NAME ${case_id}-native-transport COMMAND server1_k10_native_transport_test ${case_id} "${CMAKE_CURRENT_BINARY_DIR}/${case_id}")
    set_tests_properties(${case_id}-native-transport PROPERTIES LABELS "server1;native;K10")
  endforeach()
  add_test(NAME K10-03-native-transport
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_thread_guard.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -Port 49814)
  set_tests_properties(K10-03-native-transport PROPERTIES LABELS "server1;native;K10")
  add_test(NAME K10-02-native-transport-lifecycle
    COMMAND pwsh -NoProfile -File "${K10_PACKET_ROOT}/run_lifecycle.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -PortA 49612 -PortB 49613)
  set_tests_properties(K10-02-native-transport-lifecycle PROPERTIES LABELS "server1;native;K10")
  foreach(source_case IN ITEMS invalid generation cached mismatch infohash magnet close)
    add_test(NAME K10-E-transport-source-${source_case}
      COMMAND server1_k10_native_transport_test --source-${source_case}
        "${CMAKE_CURRENT_BINARY_DIR}/K10-E-${source_case}")
    set_tests_properties(K10-E-transport-source-${source_case} PROPERTIES
      LABELS "server1;native;K10-E")
  endforeach()
  set(K10_G_PACKET_ROOT "${SERVER1_K10_ROOT}/../../artifacts/server1/K10/K10-G")
  add_test(NAME K10-G-transport-pause-generation
    COMMAND pwsh -NoProfile -File "${K10_G_PACKET_ROOT}/run_pause.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>"
      -PortA 49819 -PortB 49820)
  set_tests_properties(K10-G-transport-pause-generation PROPERTIES
    LABELS "server1;native;K10-G")
  set(K10_H_PACKET_ROOT "${SERVER1_K10_ROOT}/../../artifacts/server1/K10/K10-H")
  add_test(NAME K10-H-transport-upload-real-wire
    COMMAND pwsh -NoProfile -File "${K10_H_PACKET_ROOT}/run_upload.ps1"
      -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>")
  set_tests_properties(K10-H-transport-upload-real-wire PROPERTIES
    LABELS "server1;native;K10-H")
endfunction()
