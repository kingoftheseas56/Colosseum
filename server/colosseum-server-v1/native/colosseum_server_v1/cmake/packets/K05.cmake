set(SERVER1_K05_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k05_packet)
  add_library(server1_k05_swarm_metadata STATIC
    "${SERVER1_K05_ROOT}/src/policy/SwarmPolicy.cpp"
    "${SERVER1_K05_ROOT}/src/policy/MetadataExchange.cpp")
  target_include_directories(server1_k05_swarm_metadata PUBLIC
    "${SERVER1_K05_ROOT}/include")
  target_link_libraries(server1_k05_swarm_metadata PRIVATE Qt6::Core)
  target_compile_features(server1_k05_swarm_metadata PUBLIC cxx_std_17)
  target_compile_options(server1_k05_swarm_metadata PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
