set(SERVER1_K12_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
function(server1_register_k12_tests)
  add_executable(server1_k12_counter_test "${SERVER1_K12_ROOT}/tests/test_engine_counters.cpp")
  target_link_libraries(server1_k12_counter_test PRIVATE server1_k12_counters Qt6::Core)
  target_compile_features(server1_k12_counter_test PRIVATE cxx_std_17)
  target_compile_options(server1_k12_counter_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
  add_test(NAME K12-counter-ownership COMMAND server1_k12_counter_test --ownership)
  set_tests_properties(K12-counter-ownership PROPERTIES LABELS "server1;native;K12"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  add_executable(server1_k12_timer_test "${SERVER1_K12_ROOT}/tests/test_engine_timer.cpp")
  target_link_libraries(server1_k12_timer_test PRIVATE server1_k12_counters Qt6::Core)
  target_compile_features(server1_k12_timer_test PRIVATE cxx_std_17)
  target_compile_options(server1_k12_timer_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
  add_test(NAME K12-real-app-lane-timer COMMAND server1_k12_timer_test)
  set_tests_properties(K12-real-app-lane-timer PROPERTIES ENVIRONMENT "QT_FATAL_WARNINGS=1" LABELS "server1;native;K12"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  # Differential oracle is explicitly supplied, hash checked, and never fetched by tests.
  set(SERVER1_K12_ORACLE "" CACHE FILEPATH "Exact locked server.js for K12 differential")
  find_program(SERVER1_NODE_EXECUTABLE node)
  if(SERVER1_K12_ORACLE AND SERVER1_NODE_EXECUTABLE)
    add_test(NAME K12-01-counter-differential COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${SERVER1_K12_ROOT}/../../tools/server_lab/oracle/k12_counter.cjs"
      "${SERVER1_K12_ORACLE}" "${SERVER1_K12_ROOT}/../../tools/server_lab/cases/K12-counter.json"
      "$<TARGET_FILE:server1_k12_counter_test>")
    set_tests_properties(K12-01-counter-differential PROPERTIES LABELS "server1;native;K12"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME K12-oracle-module-loader COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${SERVER1_K12_ROOT}/../../tools/server_lab/tests/test_module_loader.cjs"
      "${SERVER1_K12_ORACLE}")
    add_test(NAME K12-oracle-registry-order COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${SERVER1_K12_ROOT}/../../tools/server_lab/oracle/k12_registry.cjs"
      "${SERVER1_K12_ORACLE}")
    add_test(NAME K12-oracle-cache-behavior COMMAND "${SERVER1_NODE_EXECUTABLE}"
      "${SERVER1_K12_ROOT}/../../tools/server_lab/oracle/k12_cache.cjs"
      "${SERVER1_K12_ORACLE}")
    set_tests_properties(K12-oracle-module-loader K12-oracle-registry-order K12-oracle-cache-behavior
      PROPERTIES LABELS "server1;oracle;K12")
  else()
    message(STATUS "K12 differential NOT REGISTERED: supply SERVER1_K12_ORACLE and Node; ownership test remains enabled")
  endif()
endfunction()
