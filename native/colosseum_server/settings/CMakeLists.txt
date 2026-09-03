cmake_minimum_required(VERSION 3.24)
project(colosseum_server_settings_cache_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(colosseum_server_settings_cache_harness
    ServerSettings.cpp
    ../cache/CachePolicy.cpp
    ../../../tests/colosseum_server_settings_cache_harness.cpp
)
target_link_libraries(colosseum_server_settings_cache_harness PRIVATE Qt6::Core Qt6::Network)
