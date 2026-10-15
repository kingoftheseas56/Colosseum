cmake_minimum_required(VERSION 3.21)

project(vinyl_bf2_related_video LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core)

set(BA0_DOMAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../ba0-domain-prototype")

add_library(vinyl_ba0_domain_bf2 STATIC
    ${BA0_DOMAIN_DIR}/VinylDomainTypes.cpp
    ${BA0_DOMAIN_DIR}/VinylDomainTypes.h
)

target_include_directories(vinyl_ba0_domain_bf2 PUBLIC ${BA0_DOMAIN_DIR})
target_link_libraries(vinyl_ba0_domain_bf2 PUBLIC Qt6::Core)

add_library(vinyl_related_video STATIC
    RelatedVideoContract.cpp
    RelatedVideoContract.h
)

target_include_directories(vinyl_related_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vinyl_related_video PUBLIC vinyl_ba0_domain_bf2 Qt6::Core)

add_executable(vinyl_related_video_harness
    test_related_video_contract.cpp
)

target_link_libraries(vinyl_related_video_harness
    PRIVATE vinyl_related_video Qt6::Core
)

enable_testing()
add_test(NAME vinyl_related_video_harness COMMAND vinyl_related_video_harness)
