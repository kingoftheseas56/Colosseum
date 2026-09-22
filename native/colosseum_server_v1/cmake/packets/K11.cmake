set(SERVER1_K11_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k11_packet)
  add_library(server1_k11_engine STATIC
    "${SERVER1_K11_ROOT}/src/policy/TorrentEngine.cpp"
    "${SERVER1_K11_ROOT}/src/policy/EngineRegistry.cpp")
  target_include_directories(server1_k11_engine PUBLIC
    "${SERVER1_K11_ROOT}/include")
  target_link_libraries(server1_k11_engine PUBLIC
    server1_k01_metadata
    server1_k02_piece_buffer
    server1_k04_scheduler_actions
    server1_k05_swarm_metadata
    server1_k10_native_transport
    server1_k08_file_reader
    server1_k06_persistent_store
    server1_k07_circular_store
    server1_k09_peer_search)
  target_compile_features(server1_k11_engine PUBLIC cxx_std_17)
  target_compile_options(server1_k11_engine PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
