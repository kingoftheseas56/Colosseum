find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)

set(FFMPEG_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared" CACHE PATH
    "Root of the FFmpeg shared development package used by Player 2")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "Player 2 FFmpeg headers are missing under ${FFMPEG_ROOT}/include")
endif()

set(_player2_ffmpeg_libraries avformat avcodec avutil swresample avfilter)
foreach(_library IN LISTS _player2_ffmpeg_libraries)
    if(NOT EXISTS "${FFMPEG_ROOT}/lib/${_library}.lib")
        message(FATAL_ERROR "Player 2 FFmpeg import library is missing: ${FFMPEG_ROOT}/lib/${_library}.lib")
    endif()
endforeach()

add_library(player2_ffmpeg INTERFACE)
target_include_directories(player2_ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
foreach(_library IN LISTS _player2_ffmpeg_libraries)
    target_link_libraries(player2_ffmpeg INTERFACE "${FFMPEG_ROOT}/lib/${_library}.lib")
endforeach()

set(_player2_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GENERATE OUTPUT "${_player2_generated_dir}/Player2BuildMarker.cpp" CONTENT [=[
#include "core/Player2BuildMarker.h"

int Colosseum::Player2::buildMarker() noexcept
{
    return BuildContractVersion;
}
]=])
add_library(player2_core STATIC
    audio/AudioNormalizer.cpp
    audio/AudioNormalizer.h
    audio/AudioTempo.cpp
    audio/AudioTempo.h
    audio/AudioPipeline.cpp
    audio/AudioPipeline.h
    audio/WASAPIAudioSink.cpp
    audio/WASAPIAudioSink.h
    core/Player2BuildMarker.h
    core/Player2Types.cpp
    core/Player2Types.h
    core/Player2StateMachine.cpp
    core/Player2StateMachine.h
    core/PlaybackGeneration.h
    core/DemuxSession.cpp
    core/DemuxSession.h
    core/PacketQueue.cpp
    core/PacketQueue.h
    core/FrameScheduler.cpp
    core/FrameScheduler.h
    core/PlaybackClock.cpp
    core/PlaybackClock.h
    core/PlaybackMetrics.cpp
    core/PlaybackMetrics.h
    core/AudioWorker.cpp
    core/AudioWorker.h
    core/TrackPolicy.cpp
    core/TrackPolicy.h
    core/SubtitlePipeline.cpp
    core/SubtitlePipeline.h
    core/Player2Session.cpp
    core/Player2Session.h
    diagnostics/PlaybackDiagnostics.cpp
    diagnostics/PlaybackDiagnostics.h
    network/HttpMediaSource.cpp
    network/HttpMediaSource.h
    network/QtHttpTransport.cpp
    network/QtHttpTransport.h
    platform/windows/DeviceRecovery.cpp
    platform/windows/DeviceRecovery.h
    host/Player2HostServices.h
    host/HarnessHostServices.cpp
    host/HarnessHostServices.h
    PlayerBackendRouter.cpp
    PlayerBackendRouter.h
    Player2Backend.cpp
    Player2Backend.h
    video/D3D11TextureRing.cpp
    video/D3D11TextureRing.h
    video/ColorHdrPolicy.cpp
    video/ColorHdrPolicy.h
    video/D3D11VideoPipeline.cpp
    video/D3D11VideoPipeline.h
    video/Player2VideoItem.cpp
    video/Player2VideoItem.h
    "${_player2_generated_dir}/Player2BuildMarker.cpp"
)
target_include_directories(player2_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(player2_core
  PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
  PRIVATE
    Qt6::Qml
    Qt6::Network
    player2_ffmpeg
    d3d11
    dxgi
    ole32
    uuid
    avrt
)

add_executable(player2_harness
    player2_harness_main.cpp
)
target_link_libraries(player2_harness PRIVATE player2_core Qt6::Gui Qt6::Qml Qt6::Quick)
set_target_properties(player2_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
qt_add_resources(player2_harness player2_harness_qml
    PREFIX "/player2"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/Harness.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/Theme.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/qmldir"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/Player2Shell.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/Player2Icon.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/SeekBar.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/TransportBar.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/TrackMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/OverflowMenu.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/SubtitleLayer.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/StatsOverlay.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/SourceDrawer.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/EpisodeBrowser.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/SkipButton.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/PauseCard.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/ShortcutsSheet.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/CloseConfirm.qml"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/Player2Browser.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../qml/player2/controls/Player2Shortcuts.js"
)

set(_player2_shell_icons play pause volume-2 volume-x rotate-ccw rotate-cw maximize minimize
    circle-alert skip-back skip-forward languages captions gauge list-video sliders-horizontal)
set(_player2_shell_icon_files "")
foreach(_icon IN LISTS _player2_shell_icons)
    list(APPEND _player2_shell_icon_files
        "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/icons/lucide/${_icon}.svg")
endforeach()
qt_add_resources(player2_harness player2_shell_icons
    PREFIX "/player2/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/icons/lucide"
    FILES ${_player2_shell_icon_files}
)

add_executable(player2_state_machine_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_state_machine_test.cpp"
)
target_link_libraries(player2_state_machine_test PRIVATE player2_core Qt6::Test)
set_target_properties(player2_state_machine_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

enable_testing()
add_test(NAME player2_state_machine_test COMMAND player2_state_machine_test)

add_executable(player2_texture_ring_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_texture_ring_test.cpp"
)
target_link_libraries(player2_texture_ring_test PRIVATE player2_core)
set_target_properties(player2_texture_ring_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_texture_ring_test COMMAND player2_texture_ring_test)

# Task 14 — orchestration seam contract (headless: HarnessHostServices with no GPU/render path).
add_executable(player2_host_services_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_host_services_test.cpp"
)
target_link_libraries(player2_host_services_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_host_services_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_host_services_test COMMAND player2_host_services_test)

# Task 17 — backend routing: which player plays this, and what a mid-flight Player 2 failure may do.
# Pure decision logic, so this runs with no window, device or media.
add_executable(player2_backend_router_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_backend_router_test.cpp"
)
target_link_libraries(player2_backend_router_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_backend_router_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_backend_router_test COMMAND player2_backend_router_test)

# Task: PGS/bitmap subtitle image conversion — pins the RGBA byte order (invisible on white subs).
add_executable(player2_subtitle_image_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_subtitle_image_test.cpp"
)
target_link_libraries(player2_subtitle_image_test PRIVATE player2_core Qt6::Core Qt6::Gui)
set_target_properties(player2_subtitle_image_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_subtitle_image_test COMMAND player2_subtitle_image_test)

# Task: subtitle cue scheduling — display gated on the playback clock, not decode-arrival.
add_executable(player2_subtitle_schedule_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_subtitle_schedule_test.cpp"
)
target_link_libraries(player2_subtitle_schedule_test PRIVATE player2_core Qt6::Core Qt6::Gui)
set_target_properties(player2_subtitle_schedule_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_subtitle_schedule_test COMMAND player2_subtitle_schedule_test)

set(_player2_fixture_dir "${CMAKE_BINARY_DIR}/player2-fixtures")
set(_player2_fixture_script
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/fixtures/make_media_fixtures.ps1")
set(_player2_fixture_manifest
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/fixtures/manifest.json")
add_custom_command(
    OUTPUT "${_player2_fixture_dir}/.complete"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${_player2_fixture_script}"
            -OutputDirectory "${_player2_fixture_dir}"
    DEPENDS "${_player2_fixture_script}" "${_player2_fixture_manifest}"
    VERBATIM
)
add_custom_target(player2_media_fixtures DEPENDS "${_player2_fixture_dir}/.complete")

add_executable(player2_demux_session_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_demux_session_test.cpp"
)
target_link_libraries(player2_demux_session_test PRIVATE player2_core Qt6::Core)
add_dependencies(player2_demux_session_test player2_media_fixtures)
set_target_properties(player2_demux_session_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_demux_session_test
    COMMAND player2_demux_session_test "${_player2_fixture_dir}"
)

add_executable(player2_video_fixture_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_video_fixture_test.cpp"
)

add_executable(player2_audio_pipeline_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_audio_pipeline_test.cpp"
)
target_link_libraries(player2_audio_pipeline_test PRIVATE
    player2_core
    player2_ffmpeg
    Qt6::Core
)
set_target_properties(player2_audio_pipeline_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_audio_pipeline_test COMMAND player2_audio_pipeline_test)

add_executable(player2_clock_scheduler_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_clock_scheduler_test.cpp"
)
target_link_libraries(player2_clock_scheduler_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_clock_scheduler_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_clock_scheduler_test COMMAND player2_clock_scheduler_test)
target_link_libraries(player2_video_fixture_test PRIVATE
    player2_core
    player2_ffmpeg
    Qt6::Gui
    Qt6::Quick
    d3d11
    dxgi
)
set_target_properties(player2_video_fixture_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(player2_seek_generation_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_seek_generation_test.cpp"
)
target_link_libraries(player2_seek_generation_test PRIVATE player2_core player2_ffmpeg Qt6::Core)
add_dependencies(player2_seek_generation_test player2_media_fixtures)
set_target_properties(player2_seek_generation_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_seek_generation_test
    COMMAND player2_seek_generation_test "${_player2_fixture_dir}"
)

add_executable(player2_audio_normalizer_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_audio_normalizer_test.cpp"
)
target_link_libraries(player2_audio_normalizer_test PRIVATE player2_core player2_ffmpeg Qt6::Core)
set_target_properties(player2_audio_normalizer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_audio_normalizer_test COMMAND player2_audio_normalizer_test)

add_executable(player2_track_policy_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_track_policy_test.cpp"
)
target_link_libraries(player2_track_policy_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_track_policy_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_track_policy_test COMMAND player2_track_policy_test)

add_executable(player2_subtitle_timing_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_subtitle_timing_test.cpp"
)
target_link_libraries(player2_subtitle_timing_test PRIVATE player2_core player2_ffmpeg Qt6::Core)
add_dependencies(player2_subtitle_timing_test player2_media_fixtures)
set_target_properties(player2_subtitle_timing_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_subtitle_timing_test
    COMMAND player2_subtitle_timing_test "${_player2_fixture_dir}"
)

add_executable(player2_http_media_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_http_media_test.cpp"
)
target_link_libraries(player2_http_media_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_http_media_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_http_media_test COMMAND player2_http_media_test)

add_executable(player2_device_recovery_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_device_recovery_test.cpp"
)
target_link_libraries(player2_device_recovery_test PRIVATE player2_core Qt6::Core)
set_target_properties(player2_device_recovery_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_device_recovery_test COMMAND player2_device_recovery_test)

add_executable(player2_packet_queue_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_packet_queue_test.cpp"
)
target_link_libraries(player2_packet_queue_test PRIVATE player2_core player2_ffmpeg Qt6::Core)
set_target_properties(player2_packet_queue_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_packet_queue_test COMMAND player2_packet_queue_test)

add_executable(player2_playback_metrics_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_playback_metrics_test.cpp"
)
target_link_libraries(player2_playback_metrics_test PRIVATE player2_core)
set_target_properties(player2_playback_metrics_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_test(NAME player2_playback_metrics_test COMMAND player2_playback_metrics_test)

add_custom_target(player2_unit_tests DEPENDS
    player2_state_machine_test
    player2_texture_ring_test
    player2_demux_session_test
    player2_audio_pipeline_test
    player2_clock_scheduler_test
    player2_seek_generation_test
    player2_audio_normalizer_test
    player2_track_policy_test
    player2_subtitle_timing_test
    player2_http_media_test
    player2_device_recovery_test
    player2_packet_queue_test
    player2_playback_metrics_test
)
