set(SERVER1_M00_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_m00_packet)
  find_package(Qt6 CONFIG REQUIRED COMPONENTS Network)

  add_library(server1_m00_process STATIC
    "${SERVER1_M00_ROOT}/src/media/ExecutableLocator.cpp"
    "${SERVER1_M00_ROOT}/src/media/ProcessDriver.cpp")
  target_include_directories(server1_m00_process PUBLIC
    "${SERVER1_M00_ROOT}/include")
  target_compile_features(server1_m00_process PUBLIC cxx_std_17)
  target_compile_options(server1_m00_process PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
  target_link_libraries(server1_m00_process PUBLIC
    Qt6::Core
    Qt6::Network)

  add_executable(server1_m00_process_tests
    "${SERVER1_M00_ROOT}/tests/test_process_driver.cpp")
  target_compile_features(server1_m00_process_tests PRIVATE cxx_std_17)
  target_compile_options(server1_m00_process_tests PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
  target_link_libraries(server1_m00_process_tests PRIVATE server1_m00_process)
endfunction()
