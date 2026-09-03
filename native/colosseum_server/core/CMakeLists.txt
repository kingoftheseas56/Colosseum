cmake_minimum_required(VERSION 3.21)
project(colosseum_server_core_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
add_executable(tst_colosseum_server_core
    tests/tst_colosseum_server_core.cpp
    ColosseumServer.cpp
    ColosseumServer.h
    HttpConnection.cpp
    HttpConnection.h
    HttpRouter.cpp
    HttpRouter.h)
target_include_directories(tst_colosseum_server_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tst_colosseum_server_core PRIVATE Qt6::Core Qt6::Network Qt6::Test)
enable_testing()
add_test(NAME colosseum_server_core COMMAND tst_colosseum_server_core)
