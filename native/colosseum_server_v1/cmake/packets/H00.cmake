function(server1_register_h00_packet)
  find_package(Qt6 CONFIG REQUIRED COMPONENTS Network)

  add_library(server1_h00_http STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/http/Connection.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/http/Router.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/http/RequestParsing.cpp")
  target_include_directories(server1_h00_http PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(server1_h00_http PUBLIC cxx_std_17)
  target_compile_options(server1_h00_http PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>"
    "$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->")
  target_link_libraries(server1_h00_http PUBLIC Qt6::Core Qt6::Network)
endfunction()
