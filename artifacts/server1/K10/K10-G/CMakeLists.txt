cmake_minimum_required(VERSION 3.24)
project(server1_k10_g_mutation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(P03_LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc")
set(P03_LIBTORRENT_ARCHIVE "C:/tools/libtorrent-2.0-msvc/lib/torrent-rasterbar.lib")
set(P03_BOOST_ROOT "C:/tools/boost-1.84.0")
set(P03_OPENSSL_ROOT "C:/tools/openssl-msvc")
set(SERVER1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../native/colosseum_server_v1")
set(K10_G_ADAPTER_SOURCE "${SERVER1_ROOT}/src/transport/LibTorrent2Adapter.cpp"
    CACHE FILEPATH "Adapter source used by K10-G mutation gate")
set(K10_G_LEDGER_SOURCE "${SERVER1_ROOT}/src/transport/ActionLedger.cpp"
    CACHE FILEPATH "Ledger source used by K10-G mutation gate")

add_library(server1_p08_torrent_transport_contract INTERFACE)
target_include_directories(server1_p08_torrent_transport_contract INTERFACE
    "${SERVER1_ROOT}/include")
add_library(server1_libtorrent2 INTERFACE)
target_include_directories(server1_libtorrent2 INTERFACE
    "${P03_LIBTORRENT_ROOT}/include" "${P03_BOOST_ROOT}" "${P03_OPENSSL_ROOT}/include")
target_compile_definitions(server1_libtorrent2 INTERFACE
    HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
target_link_libraries(server1_libtorrent2 INTERFACE
    "${P03_LIBTORRENT_ARCHIVE}" "${P03_OPENSSL_ROOT}/lib/libssl.lib"
    "${P03_OPENSSL_ROOT}/lib/libcrypto.lib" ws2_32 mswsock crypt32 iphlpapi bcrypt)

add_library(server1_k10_g_transport STATIC
    "${K10_G_ADAPTER_SOURCE}"
    "${SERVER1_ROOT}/src/transport/PeerPlugin.cpp"
    "${K10_G_LEDGER_SOURCE}")
target_include_directories(server1_k10_g_transport PUBLIC "${SERVER1_ROOT}/include")
target_link_libraries(server1_k10_g_transport PUBLIC
    server1_p08_torrent_transport_contract server1_libtorrent2)
target_compile_options(server1_k10_g_transport PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

add_executable(server1_k10_native_transport_test "${SERVER1_ROOT}/tests/test_native_transport.cpp")
target_link_libraries(server1_k10_native_transport_test PRIVATE server1_k10_g_transport)
target_compile_options(server1_k10_native_transport_test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")
include(CTest)
add_test(NAME K10-G-mutation-behavior
    COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_LIST_DIR}/run_pause.ps1"
        -BuildDir "$<TARGET_FILE_DIR:server1_k10_native_transport_test>" -PortA 49919 -PortB 49920)
