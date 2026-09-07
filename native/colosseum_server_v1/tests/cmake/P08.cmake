function(server1_register_p08_tests)
  add_test(NAME server1_p08_transport_probe_capabilities
    COMMAND server1_p08_transport_probe --capabilities
      "${CMAKE_CURRENT_BINARY_DIR}/P08-NATIVE-CAPABILITY-MATRIX.json")
endfunction()
