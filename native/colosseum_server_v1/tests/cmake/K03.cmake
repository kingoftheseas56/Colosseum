set(SERVER1_K03_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k03_tests)
  add_executable(server1_k03_scheduler_selections_test
    "${SERVER1_K03_ROOT}/tests/test_scheduler_selections.cpp")
  target_link_libraries(server1_k03_scheduler_selections_test PRIVATE
    server1_k03_scheduler_selections)
  target_compile_features(server1_k03_scheduler_selections_test PRIVATE cxx_std_17)
  target_compile_options(server1_k03_scheduler_selections_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K03-01 K03-02 K03-03)
    add_test(NAME ${case_id}-scheduler-selections
      COMMAND server1_k03_scheduler_selections_test ${case_id})
    set_tests_properties(${case_id}-scheduler-selections PROPERTIES
      LABELS "server1;unit;K03")
  endforeach()
endfunction()
