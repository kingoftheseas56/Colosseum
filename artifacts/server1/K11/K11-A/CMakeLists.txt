cmake_minimum_required(VERSION 3.24)
project(server1_k11_packet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(P03_LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc")
set(P03_LIBTORRENT_ARCHIVE "C:/tools/libtorrent-2.0-msvc/lib/torrent-rasterbar.lib")
set(P03_BOOST_ROOT "C:/tools/boost-1.84.0")
set(P03_OPENSSL_ROOT "C:/tools/openssl-msvc")
set(SERVER1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../native/colosseum_server_v1")

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)
include(CTest)

add_library(server1_k00_policy STATIC
  "${SERVER1_ROOT}/src/policy/Value.cpp"
  "${SERVER1_ROOT}/src/policy/JsConversions.cpp")
target_include_directories(server1_k00_policy PUBLIC "${SERVER1_ROOT}/include")
target_compile_features(server1_k00_policy PUBLIC cxx_std_17)

add_library(server1_k01_metadata STATIC
  "${SERVER1_ROOT}/src/policy/TorrentMetadata.cpp"
  "${SERVER1_ROOT}/src/policy/VirtualPieceMap.cpp")
target_include_directories(server1_k01_metadata PUBLIC "${SERVER1_ROOT}/include")
target_link_libraries(server1_k01_metadata PUBLIC server1_k00_policy)
target_compile_features(server1_k01_metadata PUBLIC cxx_std_17)

include("${SERVER1_ROOT}/cmake/packets/K02.cmake")
server1_register_k02_packet()
include("${SERVER1_ROOT}/cmake/packets/K03.cmake")
server1_register_k03_packet()
include("${SERVER1_ROOT}/cmake/packets/K04.cmake")
server1_register_k04_packet()
include("${SERVER1_ROOT}/cmake/packets/K05.cmake")
server1_register_k05_packet()
include("${SERVER1_ROOT}/cmake/packets/K06.cmake")
server1_register_k06_packet()
include("${SERVER1_ROOT}/cmake/packets/K07.cmake")
server1_register_k07_packet()
include("${SERVER1_ROOT}/cmake/packets/K09.cmake")
server1_register_k09_packet()

add_library(server1_p08_torrent_transport_contract INTERFACE)
target_include_directories(server1_p08_torrent_transport_contract INTERFACE
  "${SERVER1_ROOT}/include")
target_link_libraries(server1_p08_torrent_transport_contract INTERFACE
  server1_k09_peer_search)

include("${SERVER1_ROOT}/cmake/packets/K08.cmake")
server1_register_k08_packet()
include("${SERVER1_ROOT}/cmake/packets/K10.cmake")
server1_register_k10_packet()
include("${SERVER1_ROOT}/cmake/packets/K11.cmake")
server1_register_k11_packet()
include("${SERVER1_ROOT}/tests/cmake/K11.cmake")
server1_register_k11_tests()
