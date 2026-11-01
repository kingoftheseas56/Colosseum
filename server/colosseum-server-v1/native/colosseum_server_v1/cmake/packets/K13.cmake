set(SERVER1_K13_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k13_packet)
  add_library(server1_k13_settings_cache STATIC
    "${SERVER1_K13_ROOT}/src/settings/SettingsStore.cpp"
    "${SERVER1_K13_ROOT}/src/settings/CachePolicy.cpp"
    "${SERVER1_K13_ROOT}/src/platform/DiskSpace.cpp")
  target_include_directories(server1_k13_settings_cache PUBLIC
    "${SERVER1_K13_ROOT}/include")
  target_link_libraries(server1_k13_settings_cache PRIVATE server1_k00_policy)
  target_compile_features(server1_k13_settings_cache PUBLIC cxx_std_17)
  target_compile_options(server1_k13_settings_cache PRIVATE
    "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
