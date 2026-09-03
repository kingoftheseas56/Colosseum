cmake_minimum_required(VERSION 3.20)
project(colosseum_server_w05 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(colosseum_server_w05
    Storage.cpp
    Storage.h
    DiscoverySession.cpp
    DiscoverySession.h
)
target_link_libraries(colosseum_server_w05 PUBLIC Qt6::Core)
target_include_directories(colosseum_server_w05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(w05_storage_discovery_harness
    tests/W05StorageDiscoveryHarness.cpp
)
target_link_libraries(w05_storage_discovery_harness PRIVATE colosseum_server_w05 Qt6::Core)
