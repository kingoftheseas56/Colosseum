cmake_minimum_required(VERSION 3.21)
project(vinyl_artist_service_composed VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Test)

get_filename_component(ARC_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PROTOTYPES_DIR "${ARC_BACKEND_DIR}/artist-live-prototypes-20261015")
set(BA0_DIR "${ARC_BACKEND_DIR}/ba0-domain-prototype")
set(BG1_DIR "${ARC_BACKEND_DIR}/bg1-catalogue-client-prototype")
set(BA5_DIR "${ARC_BACKEND_DIR}/ba5-library-playlists-prototype")
set(BF2_DIR "${ARC_BACKEND_DIR}/bf2-related-video-prototype")

# Compile the five prototypes directly from their existing directories without copying or forking.
add_library(vinyl_artist_service_composed STATIC
    IArtistSource.h
    FakeArtistSource.h
    FakeArtistSource.cpp
    ArtistReadAdapter.h
    ArtistReadAdapter.cpp
    SavedArtistAdapter.h
    SavedArtistAdapter.cpp
    RecordingPlaybackPort.h
    RecordingPlaybackPort.cpp
    ArtistServiceFactory.h
    ArtistServiceFactory.cpp
    "${PROTOTYPES_DIR}/catalogue/ArtistCatalogueQuery.h"
    "${PROTOTYPES_DIR}/catalogue/ArtistCatalogueQuery.cpp"
    "${PROTOTYPES_DIR}/artwork/ArtistArtworkResolver.h"
    "${PROTOTYPES_DIR}/artwork/ArtistArtworkResolver.cpp"
    "${PROTOTYPES_DIR}/personal/ArtistPersonalState.h"
    "${PROTOTYPES_DIR}/personal/ArtistPersonalState.cpp"
    "${PROTOTYPES_DIR}/media/ArtistMediaQuery.h"
    "${PROTOTYPES_DIR}/media/ArtistMediaQuery.cpp"
    "${PROTOTYPES_DIR}/bridge/ArtistServiceBridge.h"
    "${PROTOTYPES_DIR}/bridge/ArtistServiceBridge.cpp"
    "${BA0_DIR}/VinylDomainTypes.cpp"
    "${BG1_DIR}/VinylCatalogueTypes.cpp"
    "${BA5_DIR}/VinylLibrary.cpp"
    "${BA5_DIR}/VinylPlaylist.cpp"
    "${BA5_DIR}/VinylPersonalStatePersistence.cpp"
    "${BF2_DIR}/RelatedVideoContract.cpp"
)

target_include_directories(vinyl_artist_service_composed PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROTOTYPES_DIR}/catalogue"
    "${PROTOTYPES_DIR}/artwork"
    "${PROTOTYPES_DIR}/personal"
    "${PROTOTYPES_DIR}/media"
    "${PROTOTYPES_DIR}/bridge"
    "${BA0_DIR}"
    "${BG1_DIR}"
    "${BA5_DIR}"
    "${BF2_DIR}"
)

if(MSVC)
    target_compile_options(vinyl_artist_service_composed PRIVATE /utf-8 /W4)
else()
    target_compile_options(vinyl_artist_service_composed PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(vinyl_artist_service_composed PUBLIC Qt6::Core)

enable_testing()

# Composed end-to-end QtTest harness
add_executable(test_artist_service_composed
    test_artist_service_composed.cpp
)
target_link_libraries(test_artist_service_composed PRIVATE
    vinyl_artist_service_composed
    Qt6::Core
    Qt6::Test
)
add_test(NAME artist_service_composed_harness COMMAND test_artist_service_composed)

# Also build and register the five original prototype test suites unchanged so one
# CTest sweep verifies both the composed service and all five standalone suites.
option(VINYL_ARTIST_COMPOSED_BUILD_ORIGINAL_SUITES
    "Build and register the five unchanged prototype suites alongside the composed suite" ON)

if(VINYL_ARTIST_COMPOSED_BUILD_ORIGINAL_SUITES)
    add_executable(vinyl_artist_catalogue_harness
        "${PROTOTYPES_DIR}/catalogue/test_artist_catalogue.cpp"
    )
    target_link_libraries(vinyl_artist_catalogue_harness PRIVATE vinyl_artist_service_composed Qt6::Core)
    add_test(NAME vinyl_artist_catalogue_harness COMMAND vinyl_artist_catalogue_harness)

    add_executable(test_artist_artwork
        "${PROTOTYPES_DIR}/artwork/test_artist_artwork.cpp"
    )
    target_link_libraries(test_artist_artwork PRIVATE vinyl_artist_service_composed)
    add_test(NAME artist_artwork_deterministic COMMAND test_artist_artwork)

    add_executable(artist_personal_harness
        "${PROTOTYPES_DIR}/personal/test_artist_personal.cpp"
    )
    target_link_libraries(artist_personal_harness PRIVATE vinyl_artist_service_composed Qt6::Core)
    add_test(NAME artist_personal_harness COMMAND artist_personal_harness)
    set_tests_properties(artist_personal_harness PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:artist_personal_harness>"
    )

    add_executable(test_arc62_artist_media
        "${PROTOTYPES_DIR}/media/test_artist_media_query.cpp"
    )
    target_link_libraries(test_arc62_artist_media PRIVATE vinyl_artist_service_composed Qt6::Core)
    add_test(NAME artist_media_query COMMAND test_arc62_artist_media)

    add_executable(test_artist_service_bridge
        "${PROTOTYPES_DIR}/bridge/test_artist_service_bridge.cpp"
    )
    target_link_libraries(test_artist_service_bridge PRIVATE vinyl_artist_service_composed Qt6::Core Qt6::Test)
    add_test(NAME artist_service_bridge_harness COMMAND test_artist_service_bridge)
endif()
