function(server1_register_m00_tests)
  add_test(NAME M00-01-process-discovery
    COMMAND server1_m00_process_tests M00-01)
  add_test(NAME M00-02-process-lifecycle
    COMMAND server1_m00_process_tests M00-02)
  add_test(NAME M00-03-remote-driver
    COMMAND server1_m00_process_tests M00-03)
  set_tests_properties(
    M00-01-process-discovery
    M00-02-process-lifecycle
    M00-03-remote-driver
    PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()
