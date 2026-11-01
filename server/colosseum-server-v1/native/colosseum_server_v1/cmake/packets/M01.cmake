set(SERVER1_M01_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
function(server1_register_m01_packet)
  add_library(server1_m01_probe STATIC
    "${SERVER1_M01_ROOT}/src/media/Probe.cpp"
    "${SERVER1_M01_ROOT}/src/media/LegacyProbe.cpp")
  target_include_directories(server1_m01_probe PUBLIC "${SERVER1_M01_ROOT}/include")
  target_compile_features(server1_m01_probe PUBLIC cxx_std_17)
  target_link_libraries(server1_m01_probe PUBLIC server1_m00_process Qt6::Core)
  add_executable(server1_m01_probe_tests "${SERVER1_M01_ROOT}/tests/test_probe.cpp")
  target_link_libraries(server1_m01_probe_tests PRIVATE server1_m01_probe)
endfunction()
