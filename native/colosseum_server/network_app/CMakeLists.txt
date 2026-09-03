cmake_minimum_required(VERSION 3.24)
project(colosseum_server_network_app_services LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)

add_library(colosseum_server_network_app STATIC NetworkAppServices.cpp)
target_include_directories(colosseum_server_network_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(colosseum_server_network_app PUBLIC Qt6::Core Qt6::Network)

enable_testing()
foreach(name proxy youtube casting local_addon network)
    add_executable(tst_${name} tests/tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE colosseum_server_network_app)
    add_test(NAME tst_${name} COMMAND tst_${name})
endforeach()
