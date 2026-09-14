set(SERVER1_K06_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k06_tests)
  add_executable(server1_k06_persistent_store_test
    "${SERVER1_K06_ROOT}/tests/test_persistent_store.cpp")
  target_link_libraries(server1_k06_persistent_store_test PRIVATE server1_k06_persistent_store)
  target_compile_features(server1_k06_persistent_store_test PRIVATE cxx_std_17)
  target_compile_options(server1_k06_persistent_store_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K06-01 K06-02 K06-03)
    add_test(NAME ${case_id}-persistent-store
      COMMAND server1_k06_persistent_store_test
        "${CMAKE_CURRENT_BINARY_DIR}/${case_id}" ${case_id})
    set_tests_properties(${case_id}-persistent-store PROPERTIES LABELS "server1;unit;K06")
  endforeach()
endfunction()
