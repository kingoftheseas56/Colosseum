set(SERVER1_K12_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
function(server1_register_k12_packet)
  add_library(server1_k12_counters STATIC "${SERVER1_K12_ROOT}/src/policy/EngineCounters.cpp"
    "${SERVER1_K12_ROOT}/src/policy/QtEngineTimer.cpp"
    "${SERVER1_K12_ROOT}/src/policy/EngineSpeedometer.cpp"
    "${SERVER1_K12_ROOT}/src/policy/EngineSpeedometerClock.cpp")
  target_include_directories(server1_k12_counters PUBLIC "${SERVER1_K12_ROOT}/include")
  target_link_libraries(server1_k12_counters PUBLIC Qt6::Core)
  target_compile_features(server1_k12_counters PUBLIC cxx_std_17)
  target_compile_options(server1_k12_counters PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
