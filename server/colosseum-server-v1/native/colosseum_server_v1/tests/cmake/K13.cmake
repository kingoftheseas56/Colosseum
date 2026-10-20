set(SERVER1_K13_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k13_tests)
  add_executable(server1_k13_settings_cache_test
    "${SERVER1_K13_ROOT}/tests/test_settings_cache.cpp")
  target_link_libraries(server1_k13_settings_cache_test PRIVATE server1_k13_settings_cache)
  target_compile_features(server1_k13_settings_cache_test PRIVATE cxx_std_17)
  target_compile_options(server1_k13_settings_cache_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  add_test(NAME K13-01-settings-cache-defaults
    COMMAND server1_k13_settings_cache_test
      "${CMAKE_CURRENT_BINARY_DIR}/K13-01-settings-cache-defaults" K13-01)
  add_test(NAME K13-02-cache-policy
    COMMAND server1_k13_settings_cache_test
      "${CMAKE_CURRENT_BINARY_DIR}/K13-02-cache-policy" K13-02)
  add_test(NAME K13-03-cache-convergence
    COMMAND server1_k13_settings_cache_test
      "${CMAKE_CURRENT_BINARY_DIR}/K13-03-cache-convergence" K13-03)
endfunction()
