cmake_minimum_required(VERSION 3.22)
project(VinylNativeFacade LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Network Qml Quick Test)
find_package(MpvQt REQUIRED)
get_filename_component(ROOT "../.." ABSOLUTE)
include("${ROOT}/native/vinyl/backend/CMakeLists.txt")
add_executable(vinyl_native_facade
    test_facade.cpp
    "${ROOT}/native/vinyl/VinylBackendSession.cpp"
    "${ROOT}/native/vinyl/VinylBackendSession.h"
    "${ROOT}/native/player/mpvitem.cpp"
    "${ROOT}/native/player/mpvitem.h"
    "${ROOT}/native/player/mpvproperties.h")
target_include_directories(vinyl_native_facade PRIVATE "${ROOT}/native")
target_link_libraries(vinyl_native_facade PRIVATE colosseum_vinyl_backend
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Test MpvQt::MpvQt)
if(MSVC)
    target_compile_options(vinyl_native_facade PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME vinyl_native_facade COMMAND vinyl_native_facade)
add_executable(vinyl_mpv_native_events
    "${ROOT}/tests/auto/player/tst_mpvitem_native_events.cpp"
    "${ROOT}/native/player/mpvitem.cpp" "${ROOT}/native/player/mpvitem.h"
    "${ROOT}/native/player/mpvproperties.h")
target_include_directories(vinyl_mpv_native_events PRIVATE "${ROOT}/native")
target_compile_definitions(vinyl_mpv_native_events PRIVATE VAULT_FIXTURES_DIR="${ROOT}/tests/fixtures/vault")
target_link_libraries(vinyl_mpv_native_events PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::Test MpvQt::MpvQt)
add_test(NAME vinyl_mpv_native_events COMMAND vinyl_mpv_native_events)
