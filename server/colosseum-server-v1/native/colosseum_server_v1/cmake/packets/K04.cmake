set(SERVER1_K04_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k04_packet)
  add_library(server1_k04_scheduler_actions STATIC
    "${SERVER1_K04_ROOT}/src/policy/SchedulerRequests.cpp"
    "${SERVER1_K04_ROOT}/src/policy/SchedulerHotswap.cpp")
  target_include_directories(server1_k04_scheduler_actions PUBLIC
    "${SERVER1_K04_ROOT}/include")
  target_link_libraries(server1_k04_scheduler_actions PUBLIC
    server1_k03_scheduler_selections)
  target_compile_features(server1_k04_scheduler_actions PUBLIC cxx_std_17)
  target_compile_options(server1_k04_scheduler_actions PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
