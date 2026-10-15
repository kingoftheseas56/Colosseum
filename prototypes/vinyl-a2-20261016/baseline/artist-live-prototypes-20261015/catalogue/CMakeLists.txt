cmake_minimum_required(VERSION 3.21)
project(vinyl_artist_catalogue_proof LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core)

# The frozen BG1 and BA0 sources are consumed read-only. All generated
# CMake, compiler and test outputs stay under this owned catalogue directory.
add_executable(vinyl_artist_catalogue_harness
    ArtistCatalogueQuery.cpp
    ArtistCatalogueQuery.h
    test_artist_catalogue.cpp
    ../../bg1-catalogue-client-prototype/VinylCatalogueTypes.cpp
    ../../ba0-domain-prototype/VinylDomainTypes.cpp
)
if(MSVC)
    target_compile_options(vinyl_artist_catalogue_harness PRIVATE /utf-8 /W4)
else()
    target_compile_options(vinyl_artist_catalogue_harness PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(vinyl_artist_catalogue_harness PRIVATE Qt6::Core)
enable_testing()
add_test(NAME vinyl_artist_catalogue_harness COMMAND vinyl_artist_catalogue_harness)
