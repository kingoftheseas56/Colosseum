find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)

set(FFMPEG_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared" CACHE PATH
    "Root of the FFmpeg shared development package used by Player 2")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "Player 2 FFmpeg headers are missing under ${FFMPEG_ROOT}/include")
endif()

set(_player2_ffmpeg_libraries avformat avcodec avutil swresample)
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
    core/Player2Session.cpp
    core/Player2Session.h
    host/Player2HostServices.h
    host/HarnessHostServices.cpp
    host/HarnessHostServices.h
    video/D3D11TextureRing.cpp
    video/D3D11TextureRing.h
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

add_custom_target(player2_unit_tests DEPENDS
    player2_state_machine_test
    player2_texture_ring_test
    player2_demux_session_test
    player2_audio_pipeline_test
)
