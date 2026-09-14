set(SERVER1_K10_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(server1_register_k10_packet)
  if(NOT TARGET server1_libtorrent2)
    add_library(server1_libtorrent2 INTERFACE)
    target_include_directories(server1_libtorrent2 INTERFACE
      "${P03_LIBTORRENT_ROOT}/include" "${P03_BOOST_ROOT}" "${P03_OPENSSL_ROOT}/include")
    target_compile_definitions(server1_libtorrent2 INTERFACE
      HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
    target_link_libraries(server1_libtorrent2 INTERFACE
      "${P03_LIBTORRENT_ARCHIVE}" "${P03_OPENSSL_ROOT}/lib/libssl.lib"
      "${P03_OPENSSL_ROOT}/lib/libcrypto.lib" ws2_32 mswsock crypt32 iphlpapi bcrypt)
  endif()
  add_library(server1_k10_native_transport STATIC
    "${SERVER1_K10_ROOT}/src/transport/LibTorrent2Adapter.cpp"
    "${SERVER1_K10_ROOT}/src/transport/PeerPlugin.cpp"
    "${SERVER1_K10_ROOT}/src/transport/ActionLedger.cpp")
  target_include_directories(server1_k10_native_transport PUBLIC "${SERVER1_K10_ROOT}/include")
  target_link_libraries(server1_k10_native_transport PUBLIC server1_p08_torrent_transport_contract server1_libtorrent2)
  target_compile_features(server1_k10_native_transport PUBLIC cxx_std_17)
  target_compile_options(server1_k10_native_transport PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
endfunction()
