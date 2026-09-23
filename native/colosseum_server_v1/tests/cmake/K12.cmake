set(SERVER1_K12_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k12_tests)
  add_executable(server1_k12_enginefs_lifecycle_test
    "${SERVER1_K12_ROOT}/tests/test_enginefs_lifecycle.cpp")
  target_link_libraries(server1_k12_enginefs_lifecycle_test PRIVATE
    server1_k12_lifecycle)
  target_compile_features(server1_k12_enginefs_lifecycle_test PRIVATE cxx_std_17)
  target_compile_options(server1_k12_enginefs_lifecycle_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  set(K12_PACKET_ROOT "${SERVER1_K12_ROOT}/../../artifacts/server1/K12/K12-A")
  foreach(case_id IN ITEMS K12-01 K12-02 K12-03)
    add_test(NAME ${case_id}-enginefs-lifecycle
      COMMAND server1_k12_enginefs_lifecycle_test ${case_id}
        "${K12_PACKET_ROOT}/cases/K12.json"
        "${K12_PACKET_ROOT}/raw/source-outputs.json")
    # K12 links K11, which links K06; the tests need Qt6Core.dll at run time.
    set_tests_properties(${case_id}-enginefs-lifecycle PROPERTIES
      LABELS "server1;native;K12"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endforeach()
endfunction()
