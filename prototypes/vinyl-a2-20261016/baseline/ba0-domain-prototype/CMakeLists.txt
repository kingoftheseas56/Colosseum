cmake_minimum_required(VERSION 3.21)

project(vinyl_ba0_domain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core)

add_library(vinyl_domain STATIC
    VinylDomainTypes.cpp
    VinylDomainTypes.h
)

target_link_libraries(vinyl_domain PUBLIC Qt6::Core)
target_include_directories(vinyl_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(vinyl_domain_harness
    test_vinyl_domain.cpp
)

target_link_libraries(vinyl_domain_harness PRIVATE vinyl_domain Qt6::Core)

enable_testing()
add_test(NAME vinyl_domain_harness COMMAND vinyl_domain_harness)
