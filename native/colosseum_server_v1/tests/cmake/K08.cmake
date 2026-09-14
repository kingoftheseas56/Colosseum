set(SERVER1_K08_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k08_tests)
  add_executable(server1_k08_file_reader_test
    "${SERVER1_K08_ROOT}/tests/test_file_reader.cpp")
  target_link_libraries(server1_k08_file_reader_test PRIVATE
    server1_k08_file_reader)
  target_compile_features(server1_k08_file_reader_test PRIVATE cxx_std_17)
  target_compile_options(server1_k08_file_reader_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  foreach(case_id IN ITEMS K08-01 K08-02 K08-03)
    add_test(NAME ${case_id}-file-reader
      COMMAND server1_k08_file_reader_test ${case_id})
    set_tests_properties(${case_id}-file-reader PROPERTIES
      LABELS "server1;unit;K08")
  endforeach()
endfunction()
