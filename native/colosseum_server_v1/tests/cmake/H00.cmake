function(server1_register_h00_tests)
  add_executable(server1_h00_http_contract
    "${CMAKE_CURRENT_SOURCE_DIR}/test_http_contract.cpp")
  target_compile_features(server1_h00_http_contract PRIVATE cxx_std_17)
  target_compile_options(server1_h00_http_contract PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
    "$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->")
  target_link_libraries(server1_h00_http_contract PRIVATE server1_h00_http)

  foreach(case_id IN ITEMS H00-01 H00-02 H00-03)
    add_test(NAME "${case_id}-http-contract"
      COMMAND server1_h00_http_contract "${case_id}")
    set_tests_properties("${case_id}-http-contract" PROPERTIES
      LABELS "unit;server1;http"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endforeach()
endfunction()
