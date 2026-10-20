set(SERVER1_K09_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k09_packet)
  add_library(server1_k09_peer_search STATIC
    "${SERVER1_K09_ROOT}/src/discovery/PeerSearch.cpp"
    "${SERVER1_K09_ROOT}/src/discovery/TrackerSource.cpp"
    "${SERVER1_K09_ROOT}/src/discovery/DhtSource.cpp"
    "${SERVER1_K09_ROOT}/src/policy/SwarmCaps.cpp")
  target_include_directories(server1_k09_peer_search PUBLIC
    "${SERVER1_K09_ROOT}/include")
  target_compile_features(server1_k09_peer_search PUBLIC cxx_std_17)
  target_compile_options(server1_k09_peer_search PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
