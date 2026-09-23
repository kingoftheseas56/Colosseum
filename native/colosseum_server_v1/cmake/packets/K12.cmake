set(SERVER1_K12_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k12_packet)
  add_library(server1_k12_lifecycle STATIC
    "${SERVER1_K12_ROOT}/src/policy/EngineCounters.cpp"
    "${SERVER1_K12_ROOT}/src/policy/EngineStatistics.cpp"
    "${SERVER1_K12_ROOT}/src/policy/FileSelection.cpp")
  target_include_directories(server1_k12_lifecycle PUBLIC
    "${SERVER1_K12_ROOT}/include")
  target_link_libraries(server1_k12_lifecycle PUBLIC
    server1_k00_policy
    server1_k11_engine)
  target_compile_features(server1_k12_lifecycle PUBLIC cxx_std_17)
  target_compile_options(server1_k12_lifecycle PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
