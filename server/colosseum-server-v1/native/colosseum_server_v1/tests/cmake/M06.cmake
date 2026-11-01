function(server1_register_m06_tests)
  # Python shebang process fixtures and the real-media executable lane are Unix-qualified.
  # Windows remains an explicit qualification gap, not a substituted mock pass.
  if(UNIX)
    add_test(NAME M06-factory-registry-routes-media
      COMMAND server1_m06_hlsv2_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hlsv2-factory.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../artifacts/server1/M01/media/tiny.mp4")
    set_tests_properties(M06-factory-registry-routes-media PROPERTIES TIMEOUT 90)
  endif()
endfunction()
