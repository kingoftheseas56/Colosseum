find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)

set(FFMPEG_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared" CACHE PATH
    "Root of the FFmpeg shared development package used by Player 2")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "Player 2 FFmpeg headers are missing under ${FFMPEG_ROOT}/include")
endif()

set(_player2_ffmpeg_libraries avformat avcodec avutil)
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
file(GENERATE OUTPUT "${_player2_generated_dir}/player2_harness_main.cpp" CONTENT [=[
#include "player2/core/Player2BuildMarker.h"

#include <QtGui/QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    return Colosseum::Player2::buildMarker() == Colosseum::Player2::BuildContractVersion ? 0 : 1;
}
]=])

add_library(player2_core STATIC
    core/Player2BuildMarker.h
    core/Player2Types.cpp
    core/Player2Types.h
    core/Player2StateMachine.cpp
    core/Player2StateMachine.h
    host/Player2HostServices.h
    "${_player2_generated_dir}/Player2BuildMarker.cpp"
)
target_include_directories(player2_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(player2_core PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    player2_ffmpeg
)

add_executable(player2_harness
    "${_player2_generated_dir}/player2_harness_main.cpp"
)
target_link_libraries(player2_harness PRIVATE player2_core Qt6::Gui)

add_executable(player2_state_machine_test
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/player2/player2_state_machine_test.cpp"
)
target_link_libraries(player2_state_machine_test PRIVATE player2_core Qt6::Test)
set_target_properties(player2_state_machine_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

enable_testing()
add_test(NAME player2_state_machine_test COMMAND player2_state_machine_test)

add_custom_target(player2_unit_tests DEPENDS player2_state_machine_test)
