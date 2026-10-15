cmake_minimum_required(VERSION 3.21)
project(arc62_artist_media_proof LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
add_library(arc62_artist_media STATIC
    ArtistMediaQuery.cpp
    ../../bf2-related-video-prototype/RelatedVideoContract.cpp
    ../../ba0-domain-prototype/VinylDomainTypes.cpp)
target_include_directories(arc62_artist_media PUBLIC
    .
    ../../bf2-related-video-prototype
    ../../ba0-domain-prototype)
target_link_libraries(arc62_artist_media PUBLIC Qt6::Core)

add_executable(test_arc62_artist_media test_artist_media_query.cpp)
target_link_libraries(test_arc62_artist_media PRIVATE arc62_artist_media)
enable_testing()
add_test(NAME artist_media_query COMMAND test_arc62_artist_media)
