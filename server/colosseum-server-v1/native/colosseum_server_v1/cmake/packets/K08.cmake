set(SERVER1_K08_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k08_packet)
  add_library(server1_k08_file_reader STATIC
    "${SERVER1_K08_ROOT}/src/policy/FileReader.cpp")
  target_include_directories(server1_k08_file_reader PUBLIC
    "${SERVER1_K08_ROOT}/include")
  target_link_libraries(server1_k08_file_reader PUBLIC
    server1_k03_scheduler_selections)
  target_compile_features(server1_k08_file_reader PUBLIC cxx_std_17)
  target_compile_options(server1_k08_file_reader PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
