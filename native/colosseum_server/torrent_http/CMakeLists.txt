cmake_minimum_required(VERSION 3.21)
project(colosseum_server_torrent_http_w07 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(torrent_http_surface_harness
    tests/torrent_http_surface_harness.cpp
    TorrentHttpSurface.cpp
    StreamProgressTracker.cpp
)
target_link_libraries(torrent_http_surface_harness PRIVATE Qt6::Core)
target_include_directories(torrent_http_surface_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})