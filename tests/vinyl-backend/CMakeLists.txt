cmake_minimum_required(VERSION 3.22)
project(colosseum_vinyl_backend_receiving LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC OFF)
get_filename_component(COLOSSEUM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LIBMPV_PREFIX "" CACHE PATH "libmpv development prefix")
set(VINYL_REAL_MEDIA "${COLOSSEUM_ROOT}/tests/lanista_capture/assets/Colosseum.mp4" CACHE FILEPATH "Required real CUE media")
set(VINYL_IDENTITY_VIDEO "${COLOSSEUM_ROOT}/tests/fixtures/vault/media/tiny.mp4" CACHE FILEPATH "Identity probe video")
set(VINYL_IDENTITY_AUDIO "${COLOSSEUM_ROOT}/tests/fixtures/vault/media/tiny-audio.m4a" CACHE FILEPATH "Identity probe audio")
foreach(fixture IN ITEMS VINYL_REAL_MEDIA VINYL_IDENTITY_VIDEO VINYL_IDENTITY_AUDIO)
    if(NOT EXISTS "${${fixture}}")
        message(FATAL_ERROR "M0R requires ${fixture}: real tests must not silently disappear")
    endif()
endforeach()
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Test)
set(MPVQT_PREFIX "" CACHE PATH "Installed MpvQt public-header prefix")
find_path(VINYL_MPVQT_INCLUDE_DIR MpvController
    HINTS "${MPVQT_PREFIX}/include/MpvQt" PATH_SUFFIXES MpvQt REQUIRED)
include("${COLOSSEUM_ROOT}/native/vinyl/backend/CMakeLists.txt")
add_library(colosseum_vinyl_backend_test_support STATIC
    ${BG1_DIR}/FakeVinylCatalogueClient.cpp
    ${COMPOSED_DIR}/FakeArtistSource.cpp)
target_link_libraries(colosseum_vinyl_backend_test_support PUBLIC colosseum_vinyl_backend)
if(MSVC)
    target_compile_options(colosseum_vinyl_backend_test_support PRIVATE /utf-8)
    add_compile_options(/utf-8)
endif()
add_executable(vinyl_w4_superbuild_link_smoke
    ${BACKEND_DIR}/w4-superbuild/superbuild_link_smoke.cpp
)
target_link_libraries(vinyl_w4_superbuild_link_smoke PRIVATE
    colosseum_vinyl_backend
    Qt6::Core
)

enable_testing()
add_test(NAME vinyl_w4_superbuild_link_smoke
    COMMAND vinyl_w4_superbuild_link_smoke)
set_tests_properties(vinyl_w4_superbuild_link_smoke PROPERTIES
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")


function(add_w4_contract target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE
        colosseum_vinyl_backend_test_support
        Qt6::Core
    )
    add_test(NAME ${target} COMMAND ${target} ${ARGN})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${LIBMPV_PREFIX}/bin"
    )
endfunction()

add_w4_contract(vinyl_w4_ba0_contract
    "${BA0_DIR}/test_vinyl_domain.cpp")
add_w4_contract(vinyl_w4_ba1_contract
    "${BA1_DIR}/test_vinyl_queue.cpp")
add_w4_contract(vinyl_w4_ba2a_contract
    "${BA2A_DIR}/test_vinyl_playback_session.cpp")
add_w4_contract(vinyl_w4_ba2b_contract
    "${BA2B_DIR}/test_playback_engine_adapter.cpp")
add_w4_contract(vinyl_w4_ba2c_contract
    "${BA2C_DIR}/test_queue_session_coordinator.cpp")
add_w4_contract(vinyl_w4_ba2c_adversarial_contract
    "${BA2C_DIR}/test_w4b_coordinator_adversarial.cpp")
add_w4_contract(vinyl_w4_ba2d_bridge_contract
    "${BA2D_DIR}/test_colosseum_mpv_bridge.cpp")
add_w4_contract(vinyl_w4_ba3_contract
    "${BA3_DIR}/test_vinyl_session_persistence.cpp")
add_w4_contract(vinyl_host_local_identity_contract
    "${COLOSSEUM_ROOT}/tests/vinyl-backend/test_host_local_identity.cpp")
add_w4_contract(vinyl_w4_ba4_ledger_contract
    "${BA4_DIR}/test_vinyl_listen_event_ledger.cpp")
add_w4_contract(vinyl_w4_ba4_persistence_contract
    "${BA4_DIR}/test_vinyl_listen_event_persistence.cpp")
add_w4_contract(vinyl_w4_ba5_library_contract
    "${BA5_DIR}/test_vinyl_library.cpp")
add_w4_contract(vinyl_w4_ba5_playlist_contract
    "${BA5_DIR}/test_vinyl_playlist.cpp")
add_w4_contract(vinyl_w4_ba5_persistence_contract
    "${BA5_DIR}/test_vinyl_personal_state_persistence.cpp")
add_w4_contract(vinyl_w4_bb1_contract
    "${BB1_DIR}/test_source_candidate_contract.cpp")
add_w4_contract(vinyl_w4_bb2_contract
    "${BB2_DIR}/test_exact_track_resolver.cpp")
add_w4_contract(vinyl_w4_bb3_contract
    "${BB3_DIR}/test_cue_whole_album.cpp")
add_w4_contract(vinyl_w4_bb4_contract
    "${BB4_DIR}/test_mpv_playback_adapter.cpp")
add_w4_contract(vinyl_w4_bb4_cue_contract
    "${BB4_DIR}/test_cue_window_handoff.cpp")
add_w4_contract(vinyl_w4_bb5_contract
    "${BB5_DIR}/test_source_resolution_session.cpp")
add_w4_contract(vinyl_w4_bc1_contract
    "${BC1_DIR}/test_timed_text.cpp")
add_w4_contract(vinyl_w4_bc2_contract
    "${BC2_DIR}/test_translation.cpp")
add_w4_contract(vinyl_w4_bc3_contract
    "${BC3_DIR}/test_annotation_meaning.cpp")
add_w4_contract(vinyl_w4_bd1_contract
    "${BD1_DIR}/test_signal_path.cpp")
add_w4_contract(vinyl_w4_bd2_contract
    "${BD2_DIR}/test_mpv_observability.cpp")
add_w4_contract(vinyl_w4_be1_contract
    "${BE1_DIR}/test_recommendation_candidate_contract.cpp")
add_w4_contract(vinyl_w4_be2_contract
    "${BE2_DIR}/test_feedback_taste_state.cpp")
add_w4_contract(vinyl_w4_be2_persistence_contract
    "${BE2_DIR}/test_feedback_taste_persistence.cpp")
add_w4_contract(vinyl_w4_be_derivation_contract
    "${BE_DERIVATION_DIR}/test_recommendation_derivation.cpp")
add_w4_contract(vinyl_w4_be3_contract
    "${BE3_DIR}/test_deterministic_ranker.cpp")
add_w4_contract(vinyl_w4_bf1_contract
    "${BF1_DIR}/test_context_graph.cpp")
add_w4_contract(vinyl_w4_bf2_contract
    "${BF2_DIR}/test_related_video_contract.cpp")
add_w4_contract(vinyl_w4_bf3_contract
    "${BF3_DIR}/test_video_playback_adapter.cpp")
add_w4_contract(vinyl_w4_bg1_contract
    "${BG1_DIR}/test_catalogue_client.cpp")
add_w4_contract(vinyl_w4_bg2_contract
    "${BG2_DIR}/test_discovery_enrichment.cpp")

target_compile_definitions(vinyl_w4_bg1_contract PRIVATE
    VINYL_BG1_SOURCE_DIR="${BG1_DIR}"
)
target_compile_definitions(vinyl_w4_bg2_contract PRIVATE
    VINYL_BG2_SOURCE_DIR="${BG2_DIR}"
)

add_w4_contract(vinyl_w4_h_fake_journey
    "${BACKEND_DIR}/w4-end-to-end-prototype/fake_play_journey.cpp")
target_sources(vinyl_w4_h_fake_journey PRIVATE
    "${BACKEND_DIR}/w4-end-to-end-prototype/W4ComposedIntegration.cpp")

add_executable(vinyl_w4_h_real_cue_journey
    "${BACKEND_DIR}/w4-end-to-end-prototype/real_cue_journey.cpp"
)
target_link_libraries(vinyl_w4_h_real_cue_journey PRIVATE
    colosseum_vinyl_backend
    Qt6::Core
)
add_test(NAME vinyl_w4_h_real_cue_journey
    COMMAND vinyl_w4_h_real_cue_journey "${VINYL_REAL_MEDIA}")
set_tests_properties(vinyl_w4_h_real_cue_journey PROPERTIES
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${LIBMPV_PREFIX}/bin"
    TIMEOUT 30)

add_w4_contract(vinyl_artist_catalogue_harness "${ARTIST_DIR}/catalogue/test_artist_catalogue.cpp")
add_w4_contract(test_artist_artwork "${ARTIST_DIR}/artwork/test_artist_artwork.cpp")
add_w4_contract(artist_personal_harness "${ARTIST_DIR}/personal/test_artist_personal.cpp")
add_w4_contract(test_arc62_artist_media "${ARTIST_DIR}/media/test_artist_media_query.cpp")
add_w4_contract(test_artist_service_bridge "${ARTIST_DIR}/bridge/test_artist_service_bridge.cpp")
add_w4_contract(test_artist_service_composed "${COMPOSED_DIR}/test_artist_service_composed.cpp")
target_link_libraries(test_artist_service_bridge PRIVATE Qt6::Test)
target_link_libraries(test_artist_service_composed PRIVATE Qt6::Test)
set_target_properties(test_artist_service_bridge test_artist_service_composed PROPERTIES AUTOMOC ON)

add_w4_contract(vinyl_ba2db2_playlist_identity_probe
    "${BA2D_DIR}/mpv_playlist_identity_probe.cpp" "${VINYL_IDENTITY_VIDEO}" "${VINYL_IDENTITY_AUDIO}")
set_tests_properties(vinyl_ba2db2_playlist_identity_probe PROPERTIES TIMEOUT 20)
add_w4_contract(vinyl_ba2db3_seek_completion_probe
    "${BA2D_DIR}/mpv_seek_completion_probe.cpp" "${VINYL_REAL_MEDIA}")
set_tests_properties(vinyl_ba2db3_seek_completion_probe PROPERTIES TIMEOUT 30)
add_w4_contract(vinyl_w4a_bounded_mpv_probe
    "${BA2D_DIR}/mpv_bounded_window_probe.cpp" "${VINYL_REAL_MEDIA}")
set_tests_properties(vinyl_w4a_bounded_mpv_probe PROPERTIES TIMEOUT 20)
add_w4_contract(vinyl_ba2d_mpv_api_probe "${BA2D_DIR}/mpv_api_probe.cpp")
target_include_directories(vinyl_ba2d_mpv_api_probe PRIVATE "${VINYL_MPVQT_INCLUDE_DIR}")
