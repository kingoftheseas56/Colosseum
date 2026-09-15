cmake_minimum_required(VERSION 3.24)
project(server1_k11_combined_aggregate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SERVER1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../native/colosseum_server_v1")

include(CTest)
add_subdirectory("${SERVER1_ROOT}" precursor)
include("${SERVER1_ROOT}/cmake/packets/K11.cmake")
server1_register_k11_packet()
include("${SERVER1_ROOT}/tests/cmake/K11.cmake")
server1_register_k11_tests()
