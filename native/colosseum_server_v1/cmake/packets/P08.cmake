function(server1_register_p08_probe)
  add_executable(server1_p08_transport_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments/transport_contract_probe.cpp")
  target_include_directories(server1_p08_transport_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/experiments"
    "${P03_LIBTORRENT_ROOT}/include"
    "${P03_BOOST_ROOT}")
  target_compile_definitions(server1_p08_transport_probe PRIVATE
    HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
  target_link_libraries(server1_p08_transport_probe PRIVATE
    "${P03_LIBTORRENT_ARCHIVE}"
    "${P03_OPENSSL_ROOT}/lib/libssl.lib"
    "${P03_OPENSSL_ROOT}/lib/libcrypto.lib"
    ws2_32 mswsock crypt32 iphlpapi bcrypt advapi32 user32)
endfunction()
