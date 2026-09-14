set(SERVER1_K07_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k07_packet)
  add_library(server1_k07_circular_store STATIC
    "${SERVER1_K07_ROOT}/src/storage/CircularPieceStore.cpp")
  target_include_directories(server1_k07_circular_store PUBLIC
    "${SERVER1_K07_ROOT}/include")
  target_compile_features(server1_k07_circular_store PUBLIC cxx_std_17)
  target_compile_options(server1_k07_circular_store PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
