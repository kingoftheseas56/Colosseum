function(server1_register_m01_tests)
  add_test(NAME M01-01-probe-normalization
    COMMAND server1_m01_probe_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe.json")
  set_tests_properties(M01-01-probe-normalization PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  if(UNIX)
    add_test(NAME M01-02-probe-process
      COMMAND server1_m01_probe_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/probe_process_fixture.py")
    add_test(NAME M01-03-legacy-timeout
      COMMAND server1_m01_probe_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/probe_process_fixture.py" --legacy-timeout)
    set_tests_properties(M01-03-legacy-timeout PROPERTIES TIMEOUT 30)
  endif()
endfunction()
