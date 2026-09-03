cmake_minimum_required(VERSION 3.24)
project(colosseum_scheduler_spine_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(scheduler_spine_tests
    scheduler_spine_tests.cpp
    ../SchedulerSpine.cpp
    ../FileStream.cpp)

target_include_directories(scheduler_spine_tests PRIVATE ..)

if(MSVC)
    target_compile_options(scheduler_spine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(scheduler_spine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME scheduler_spine_tests COMMAND scheduler_spine_tests)
