set(SERVER1_K11_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k11_tests)
  add_executable(server1_k11_engine_registry_test
    "${SERVER1_K11_ROOT}/tests/test_engine_registry.cpp")
  target_link_libraries(server1_k11_engine_registry_test PRIVATE
    server1_k11_engine)
  target_compile_features(server1_k11_engine_registry_test PRIVATE cxx_std_17)
  target_compile_options(server1_k11_engine_registry_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K11-01 K11-02 K11-03)
    add_test(NAME ${case_id}-engine-registry
      COMMAND server1_k11_engine_registry_test ${case_id})
    set_tests_properties(${case_id}-engine-registry PROPERTIES
      LABELS "server1;native;K11")
  endforeach()
endfunction()
