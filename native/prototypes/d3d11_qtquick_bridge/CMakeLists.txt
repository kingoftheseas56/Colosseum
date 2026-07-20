cmake_minimum_required(VERSION 3.20)
project(d3d11_qtquick_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(slot_ring_test
    tests/slot_ring_test.cpp
)
target_include_directories(slot_ring_test PRIVATE src)

add_test(NAME slot_ring_test COMMAND slot_ring_test)

