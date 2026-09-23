# Packet-local verification aggregate: the accepted Server 1.0 aggregate plus
# K12. INT-W3 alone registers K12 in native/colosseum_server_v1 (B-W3F).
cmake_minimum_required(VERSION 3.24)
project(server1_k12_combined_aggregate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SERVER1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../native/colosseum_server_v1")

include(CTest)
# The K12 test manifest reads Qt6::Core; imported targets from the
# subdirectory are not visible here.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)
add_subdirectory("${SERVER1_ROOT}" precursor)
include("${SERVER1_ROOT}/cmake/packets/K12.cmake")
server1_register_k12_packet()
include("${SERVER1_ROOT}/tests/cmake/K12.cmake")
server1_register_k12_tests()
