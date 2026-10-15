cmake_minimum_required(VERSION 3.21)
project(artist_personal_proof LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 REQUIRED COMPONENTS Core)
get_filename_component(ARC_BACKEND "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BA0 "${ARC_BACKEND}/ba0-domain-prototype")
set(BA5 "${ARC_BACKEND}/ba5-library-playlists-prototype")
add_library(artist_personal STATIC
    ArtistPersonalState.cpp ArtistPersonalState.h
    "${BA0}/VinylDomainTypes.cpp"
    "${BA5}/VinylLibrary.cpp"
    "${BA5}/VinylPlaylist.cpp"
    "${BA5}/VinylPersonalStatePersistence.cpp")
target_include_directories(artist_personal PUBLIC "${BA0}" "${BA5}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(artist_personal PUBLIC Qt6::Core)
add_executable(artist_personal_harness test_artist_personal.cpp)
target_link_libraries(artist_personal_harness PRIVATE artist_personal)
enable_testing()
add_test(NAME artist_personal_harness COMMAND artist_personal_harness)
set_tests_properties(artist_personal_harness PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:artist_personal_harness>")
