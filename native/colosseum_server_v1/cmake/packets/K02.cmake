set(SERVER1_K02_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k02_packet)
  add_library(server1_k02_piece_buffer STATIC
    "${SERVER1_K02_ROOT}/src/policy/PieceBuffer.cpp")
  target_include_directories(server1_k02_piece_buffer PUBLIC
    "${SERVER1_K02_ROOT}/include")
  target_compile_features(server1_k02_piece_buffer PUBLIC cxx_std_17)
  target_compile_options(server1_k02_piece_buffer PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
