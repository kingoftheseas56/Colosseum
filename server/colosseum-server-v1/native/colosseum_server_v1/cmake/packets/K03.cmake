set(SERVER1_K03_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k03_packet)
  add_library(server1_k03_scheduler_selections STATIC
    "${SERVER1_K03_ROOT}/src/policy/SchedulerSelections.cpp")
  target_include_directories(server1_k03_scheduler_selections PUBLIC
    "${SERVER1_K03_ROOT}/include")
  target_link_libraries(server1_k03_scheduler_selections PUBLIC server1_k00_policy)
  target_compile_features(server1_k03_scheduler_selections PUBLIC cxx_std_17)
  target_compile_options(server1_k03_scheduler_selections PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
