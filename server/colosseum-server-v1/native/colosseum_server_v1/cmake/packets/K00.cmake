function(server1_register_k00_packet)
  add_library(server1_k00_policy STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/policy/Value.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/policy/JsConversions.cpp")
  target_include_directories(server1_k00_policy PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(server1_k00_policy PUBLIC cxx_std_17)
  target_compile_options(server1_k00_policy PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  add_executable(server1_k00_js_semantics
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_js_semantics.cpp")
  target_link_libraries(server1_k00_js_semantics PRIVATE server1_k00_policy)
  target_compile_features(server1_k00_js_semantics PRIVATE cxx_std_17)
  target_compile_options(server1_k00_js_semantics PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
