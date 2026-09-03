cmake_minimum_required(VERSION 3.21)
project(colosseum_server_media_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(MEDIA_DIR "${REPO_ROOT}/native/colosseum_server/media")

add_executable(colosseum_server_media_harness
    media_pipeline_harness.cpp
    "${MEDIA_DIR}/MediaPipeline.cpp"
    "${MEDIA_DIR}/MediaProbe.cpp"
    "${MEDIA_DIR}/TrackParser.cpp"
    "${MEDIA_DIR}/SubtitleService.cpp"
    "${MEDIA_DIR}/LegacyHls.cpp"
    "${MEDIA_DIR}/HlsV2.cpp"
    "${MEDIA_DIR}/EmbeddedSamples.cpp")

target_include_directories(colosseum_server_media_harness PRIVATE
    "${REPO_ROOT}/native")
target_compile_definitions(colosseum_server_media_harness PRIVATE
    MEDIA_FIXTURES_DIR="${REPO_ROOT}/tests/fixtures/colosseum_server_media")
target_link_libraries(colosseum_server_media_harness PRIVATE Qt6::Core Qt6::Network)
