set(SERVER1_K09_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k09_tests)
  add_executable(server1_k09_peer_search_test
    "${SERVER1_K09_ROOT}/tests/test_peer_search.cpp")
  target_link_libraries(server1_k09_peer_search_test PRIVATE
    server1_k09_peer_search)
  target_compile_features(server1_k09_peer_search_test PRIVATE cxx_std_17)
  target_compile_options(server1_k09_peer_search_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K09-01 K09-02 K09-03)
    add_test(NAME ${case_id}-peer-search COMMAND server1_k09_peer_search_test ${case_id})
    set_tests_properties(${case_id}-peer-search PROPERTIES
      LABELS "server1;unit;K09")
  endforeach()
endfunction()
