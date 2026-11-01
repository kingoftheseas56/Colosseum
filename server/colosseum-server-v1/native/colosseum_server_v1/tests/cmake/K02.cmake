set(SERVER1_K02_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k02_tests)
  add_executable(server1_k02_piece_buffer_test
    "${SERVER1_K02_ROOT}/tests/test_piece_buffer.cpp")
  target_link_libraries(server1_k02_piece_buffer_test PRIVATE server1_k02_piece_buffer)
  target_compile_features(server1_k02_piece_buffer_test PRIVATE cxx_std_17)
  target_compile_options(server1_k02_piece_buffer_test PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  add_test(NAME K02-01-piece-buffer COMMAND server1_k02_piece_buffer_test K02-01)
  add_test(NAME K02-02-piece-buffer COMMAND server1_k02_piece_buffer_test K02-02)
  add_test(NAME K02-03-piece-buffer COMMAND server1_k02_piece_buffer_test K02-03)
  set_tests_properties(K02-01-piece-buffer K02-02-piece-buffer K02-03-piece-buffer
    PROPERTIES LABELS "server1;unit;K02")
endfunction()
