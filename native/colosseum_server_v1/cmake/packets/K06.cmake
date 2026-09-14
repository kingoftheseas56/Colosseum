set(SERVER1_K06_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k06_packet)
  add_library(server1_k06_persistent_store STATIC
    "${SERVER1_K06_ROOT}/src/storage/PersistentPieceStore.cpp"
    "${SERVER1_K06_ROOT}/src/storage/VerificationBitmap.cpp")
  target_include_directories(server1_k06_persistent_store PUBLIC
    "${SERVER1_K06_ROOT}/include")
  target_link_libraries(server1_k06_persistent_store PUBLIC server1_k02_piece_buffer Qt6::Core)
  target_compile_features(server1_k06_persistent_store PUBLIC cxx_std_17)
  target_compile_options(server1_k06_persistent_store PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
