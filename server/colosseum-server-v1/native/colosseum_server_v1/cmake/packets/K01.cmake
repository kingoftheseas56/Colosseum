function(server1_register_k01_packet)
  add_library(server1_k01_metadata STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/policy/TorrentMetadata.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/policy/VirtualPieceMap.cpp")
  target_include_directories(server1_k01_metadata PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(server1_k01_metadata PUBLIC server1_k00_policy)
  target_compile_features(server1_k01_metadata PUBLIC cxx_std_17)
  target_compile_options(server1_k01_metadata PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

  add_executable(server1_k01_metadata_geometry
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_metadata_geometry.cpp")
  target_link_libraries(server1_k01_metadata_geometry PRIVATE server1_k01_metadata)
  target_compile_features(server1_k01_metadata_geometry PRIVATE cxx_std_17)
  target_compile_options(server1_k01_metadata_geometry PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
