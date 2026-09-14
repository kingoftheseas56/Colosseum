set(SERVER1_K04_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k04_tests)
  add_executable(server1_k04_scheduler_actions_test
    "${SERVER1_K04_ROOT}/tests/test_scheduler_requests.cpp")
  target_include_directories(server1_k04_scheduler_actions_test PRIVATE
    "${SERVER1_K04_ROOT}/include")
  target_compile_features(server1_k04_scheduler_actions_test PRIVATE cxx_std_17)
  target_compile_options(server1_k04_scheduler_actions_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K04-01 K04-02 K04-03)
    add_test(NAME ${case_id}-scheduler-actions COMMAND server1_k04_scheduler_actions_test ${case_id})
    set_tests_properties(${case_id}-scheduler-actions PROPERTIES
      LABELS "server1;unit;K04")
  endforeach()
endfunction()
