cmake_minimum_required(VERSION 3.22)

project(vinyl_ba5_library_playlists LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core)

set(BA0_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ba0-domain-prototype")

add_library(vinyl_domain STATIC
    ${BA0_DIR}/VinylDomainTypes.cpp
    ${BA0_DIR}/VinylDomainTypes.h
)
target_include_directories(vinyl_domain PUBLIC ${BA0_DIR})
target_link_libraries(vinyl_domain PUBLIC Qt6::Core)

add_library(vinyl_library STATIC
    VinylLibrary.cpp
    VinylLibrary.h
)
target_include_directories(vinyl_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vinyl_library PUBLIC vinyl_domain Qt6::Core)

add_executable(vinyl_library_harness
    test_vinyl_library.cpp
)
target_link_libraries(vinyl_library_harness PRIVATE vinyl_library Qt6::Core)

add_library(vinyl_playlist STATIC
    VinylPlaylist.cpp
    VinylPlaylist.h
)
target_include_directories(vinyl_playlist PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vinyl_playlist PUBLIC vinyl_library vinyl_domain Qt6::Core)

set(BA1_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ba1-queue-core-prototype")

add_executable(vinyl_playlist_harness
    test_vinyl_playlist.cpp
)
target_include_directories(vinyl_playlist_harness PRIVATE ${BA1_DIR})
target_link_libraries(vinyl_playlist_harness PRIVATE vinyl_playlist Qt6::Core)

add_library(vinyl_personal_state_persistence STATIC
    VinylPersonalStatePersistence.cpp
    VinylPersonalStatePersistence.h
)
target_link_libraries(vinyl_personal_state_persistence PUBLIC vinyl_playlist vinyl_library Qt6::Core)

add_executable(vinyl_personal_state_persistence_harness
    test_vinyl_personal_state_persistence.cpp
)
target_link_libraries(vinyl_personal_state_persistence_harness PRIVATE
    vinyl_personal_state_persistence Qt6::Core
)

enable_testing()
add_test(NAME vinyl_library_harness COMMAND vinyl_library_harness)
add_test(NAME vinyl_playlist_harness COMMAND vinyl_playlist_harness)
add_test(NAME vinyl_personal_state_persistence_harness
         COMMAND vinyl_personal_state_persistence_harness)
set_tests_properties(
    vinyl_library_harness
    vinyl_playlist_harness
    vinyl_personal_state_persistence_harness
    PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
