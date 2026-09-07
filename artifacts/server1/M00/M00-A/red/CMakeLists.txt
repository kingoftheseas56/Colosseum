cmake_minimum_required(VERSION 3.24)
project(m00_red LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)

add_executable(m00_red_process_test
  "C:/b/Colosseum-Server-1.0-workers/M00-A/native/colosseum_server_v1/tests/test_process_driver.cpp")
target_include_directories(m00_red_process_test PRIVATE
  "C:/b/Colosseum-Server-1.0-workers/M00-A/native/colosseum_server_v1/include")
target_link_libraries(m00_red_process_test PRIVATE Qt6::Core)
