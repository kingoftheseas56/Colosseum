cmake_minimum_required(VERSION 3.24)
project(colosseum_server_integration_smoke LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Test Gui Quick)

if(WIN32)
    set(_W40_MPVQT_ROOT "C:/tools/mpvqt-feasibility/mpvqt-msvc-install")
    set(_W40_LIBMPV_ROOT "C:/tools/mpvqt-feasibility/libmpv-prefix")
    list(PREPEND CMAKE_PREFIX_PATH "${_W40_MPVQT_ROOT}")
    set(MpvQt_DIR "${_W40_MPVQT_ROOT}/lib/cmake/MpvQt")
    set(Libmpv_INCLUDE_DIRS "${_W40_LIBMPV_ROOT}/include")
    set(Libmpv_LIBRARIES "${_W40_LIBMPV_ROOT}/lib/mpv.lib")
endif()
find_package(MpvQt QUIET)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." colosseum_server_production)

add_executable(colosseum_server_integration_smoke integration_smoke.cpp)
target_link_libraries(colosseum_server_integration_smoke PRIVATE colosseum_server_native)

enable_testing()
add_test(NAME colosseum_server_integration_smoke COMMAND colosseum_server_integration_smoke)

add_executable(scheduler_transport_bridge_test transport_bridge_test.cpp)
target_link_libraries(scheduler_transport_bridge_test PRIVATE colosseum_server_native)
add_test(NAME scheduler_transport_bridge_test COMMAND scheduler_transport_bridge_test)

add_executable(piece_source_block_transport_test piece_source_transport_test.cpp)
target_link_libraries(piece_source_block_transport_test PRIVATE colosseum_server_native)
add_test(NAME piece_source_block_transport_test COMMAND piece_source_block_transport_test)

add_executable(libtorrent_block_transport_test
    libtorrent_block_transport_test.cpp)
target_link_libraries(libtorrent_block_transport_test PRIVATE colosseum_server_native)
add_test(NAME libtorrent_block_transport_test COMMAND libtorrent_block_transport_test)

add_executable(torrent_piece_source_test
    torrent_piece_source_test.cpp)
target_link_libraries(torrent_piece_source_test PRIVATE colosseum_server_native)
add_test(NAME torrent_piece_source_test COMMAND torrent_piece_source_test)

add_executable(torrent_http_wire_integration_test
    torrent_http_wire_integration_test.cpp)
target_link_libraries(torrent_http_wire_integration_test PRIVATE colosseum_server_native)
add_test(NAME torrent_http_wire_integration_test
         COMMAND torrent_http_wire_integration_test)

add_executable(feature_route_composition_test
    feature_route_composition_test.cpp)
target_link_libraries(feature_route_composition_test PRIVATE colosseum_server_native)
add_test(NAME feature_route_composition_test
         COMMAND feature_route_composition_test)

add_executable(runtime_composition_test
    runtime_composition_test.cpp
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_link_libraries(runtime_composition_test PRIVATE colosseum_server_native)
add_test(NAME runtime_composition_test
         COMMAND runtime_composition_test)

add_executable(streamserver_native_cutover_test
    streamserver_native_cutover_test.cpp
    ../../player/streamserver.cpp
    ../../player/streamserver.h
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_include_directories(streamserver_native_cutover_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../..")
target_link_libraries(streamserver_native_cutover_test PRIVATE
    colosseum_server_native Qt6::Test)
add_test(NAME streamserver_native_cutover_test
         COMMAND streamserver_native_cutover_test)

add_executable(streamserver_readiness_test
    streamserver_readiness_test.cpp
    ../../player/streamserver.cpp
    ../../player/streamserver.h
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_include_directories(streamserver_readiness_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../..")
target_link_libraries(streamserver_readiness_test PRIVATE
    colosseum_server_native Qt6::Network Qt6::Test)
add_test(NAME streamserver_readiness_test
         COMMAND streamserver_readiness_test)

add_executable(runtime_torrent_http_e2e_test
    runtime_torrent_http_e2e_test.cpp
    ../../player/streamserver.cpp
    ../../player/streamserver.h
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_include_directories(runtime_torrent_http_e2e_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../..")
target_link_libraries(runtime_torrent_http_e2e_test PRIVATE
    colosseum_server_native Qt6::Network Qt6::Test)
add_test(NAME runtime_torrent_http_e2e_test
         COMMAND runtime_torrent_http_e2e_test)

add_executable(runtime_lifecycle_test
    runtime_lifecycle_test.cpp
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_link_libraries(runtime_lifecycle_test PRIVATE colosseum_server_native)
add_test(NAME runtime_lifecycle_test COMMAND runtime_lifecycle_test)

add_executable(runtime_restart_stress_test
    runtime_restart_stress_test.cpp
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_link_libraries(runtime_restart_stress_test PRIVATE colosseum_server_native)
add_test(NAME runtime_restart_stress_test COMMAND runtime_restart_stress_test)

add_executable(runtime_active_request_shutdown_test
    runtime_active_request_shutdown_test.cpp
    ../../torrent/engine/DebugLogBuffer.cpp
    ../../torrent/engine/DebugLogBuffer.h)
target_link_libraries(runtime_active_request_shutdown_test PRIVATE colosseum_server_native)
add_test(NAME runtime_active_request_shutdown_test COMMAND runtime_active_request_shutdown_test)

if(TARGET MpvQt::MpvQt)
    add_executable(runtime_mpv_playback_test
        runtime_mpv_playback_test.cpp
        ../../player/mpvitem.cpp
        ../../player/mpvitem.h
        ../../player/mpvproperties.h
        ../../player/http_header_fields.h
        ../../player/streamserver.cpp
        ../../player/streamserver.h
        ../../torrent/engine/DebugLogBuffer.cpp
        ../../torrent/engine/DebugLogBuffer.h)
    target_include_directories(runtime_mpv_playback_test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../..")
    target_compile_definitions(runtime_mpv_playback_test PRIVATE
        W40_MEDIA_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/../../../tests/fixtures/vault/media/tiny.mp4")
    target_link_libraries(runtime_mpv_playback_test PRIVATE
        colosseum_server_native Qt6::Gui Qt6::Quick Qt6::Test MpvQt::MpvQt)
    add_test(NAME runtime_mpv_playback_test COMMAND runtime_mpv_playback_test)
    set_tests_properties(runtime_mpv_playback_test PROPERTIES
        LABELS "integration;player;mpv"
        TIMEOUT 120)
else()
    message(STATUS "MpvQt not found; native runtime_mpv_playback_test is not configured")
endif()

add_executable(async_media_executor_lifecycle_test
    async_media_executor_lifecycle_test.cpp)
target_link_libraries(async_media_executor_lifecycle_test PRIVATE
    colosseum_server_native Qt6::Test)
add_test(NAME async_media_executor_lifecycle_test COMMAND async_media_executor_lifecycle_test)

add_executable(media_executable_locator_test
    media_executable_locator_test.cpp)
target_link_libraries(media_executable_locator_test PRIVATE colosseum_server_native)
add_test(NAME media_executable_locator_test COMMAND media_executable_locator_test)