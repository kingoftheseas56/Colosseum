cmake_minimum_required(VERSION 3.25)
project(server1_k08_packet LANGUAGES CXX)

set(SERVER1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../native/colosseum_server_v1")
find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core)

add_library(server1_k00_policy STATIC
  "${SERVER1_ROOT}/src/policy/Value.cpp"
  "${SERVER1_ROOT}/src/policy/JsConversions.cpp")
target_include_directories(server1_k00_policy PUBLIC "${SERVER1_ROOT}/include")
target_compile_features(server1_k00_policy PUBLIC cxx_std_17)
target_compile_options(server1_k00_policy PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/EHsc>")

include("${SERVER1_ROOT}/cmake/packets/K03.cmake")
include("${SERVER1_ROOT}/cmake/packets/K08.cmake")
include("${SERVER1_ROOT}/tests/cmake/K08.cmake")

server1_register_k03_packet()
server1_register_k08_packet()
enable_testing()
server1_register_k08_tests()
