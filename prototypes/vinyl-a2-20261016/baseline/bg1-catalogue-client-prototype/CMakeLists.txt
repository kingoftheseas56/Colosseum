cmake_minimum_required(VERSION 3.21)

project(vinyl_bg1_catalogue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(vinyl_bg1_catalogue STATIC
    VinylCatalogueTypes.cpp
    VinylCatalogueTypes.h
    VinylCatalogueClient.h
    FakeVinylCatalogueClient.cpp
    FakeVinylCatalogueClient.h
    ../ba0-domain-prototype/VinylDomainTypes.cpp
    ../ba0-domain-prototype/VinylDomainTypes.h
)

target_include_directories(vinyl_bg1_catalogue PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../ba0-domain-prototype
)
target_link_libraries(vinyl_bg1_catalogue PUBLIC Qt6::Core)

add_executable(vinyl_bg1_catalogue_harness
    test_catalogue_client.cpp
)
target_compile_definitions(vinyl_bg1_catalogue_harness PRIVATE
    VINYL_BG1_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(vinyl_bg1_catalogue_harness PRIVATE
    vinyl_bg1_catalogue
    Qt6::Core
)

enable_testing()
add_test(NAME vinyl_bg1_catalogue_harness
         COMMAND vinyl_bg1_catalogue_harness)
