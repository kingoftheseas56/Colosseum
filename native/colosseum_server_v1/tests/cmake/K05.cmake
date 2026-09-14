set(SERVER1_K05_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k05_tests)
  add_executable(server1_k05_swarm_metadata_test
    "${SERVER1_K05_ROOT}/tests/test_swarm_metadata.cpp")
  target_include_directories(server1_k05_swarm_metadata_test PRIVATE
    "${SERVER1_K05_ROOT}/include")
  target_link_libraries(server1_k05_swarm_metadata_test PRIVATE Qt6::Core)
  target_compile_features(server1_k05_swarm_metadata_test PRIVATE cxx_std_17)
  target_compile_options(server1_k05_swarm_metadata_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  add_test(NAME K05-01-swarm-metadata COMMAND server1_k05_swarm_metadata_test K05-01)
  add_test(NAME K05-02-swarm-metadata COMMAND server1_k05_swarm_metadata_test K05-02)
  add_test(NAME K05-03-swarm-metadata COMMAND server1_k05_swarm_metadata_test K05-03)
  set_tests_properties(K05-01-swarm-metadata K05-02-swarm-metadata K05-03-swarm-metadata
    PROPERTIES LABELS "server1;unit;K05")
endfunction()
