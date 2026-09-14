set(SERVER1_K07_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k07_tests)
  add_executable(server1_k07_circular_store_test
    "${SERVER1_K07_ROOT}/tests/test_circular_store.cpp")
  target_include_directories(server1_k07_circular_store_test PRIVATE
    "${SERVER1_K07_ROOT}/include")
  target_compile_features(server1_k07_circular_store_test PRIVATE cxx_std_17)
  target_compile_options(server1_k07_circular_store_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K07-01 K07-02 K07-03)
    add_test(NAME ${case_id}-circular-store
      COMMAND server1_k07_circular_store_test
        "${CMAKE_CURRENT_BINARY_DIR}/${case_id}" ${case_id})
    set_tests_properties(${case_id}-circular-store PROPERTIES
      LABELS "server1;unit;K07")
  endforeach()
endfunction()
