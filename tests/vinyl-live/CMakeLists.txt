cmake_minimum_required(VERSION 3.21)
project(vinyl_live_harness LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network QuickTest Test)
find_package(MpvQt REQUIRED)
add_library(vinyl_torrent_support STATIC
    ../../native/vinyl/VinylTorrentProvider.cpp
    ../../native/bootstrap/ChildProcessJob.cpp ../../native/bootstrap/ChildProcessJob.h ../../native/player/streamserver.cpp
    ../../native/torrent/MusicTorrentMatcher.cpp
    ../../native/torrent/MusicTorrentQueryPlanner.cpp
    ../../native/torrent/TankorentSearchService.cpp
    ../../native/torrent/TankorentWorkerIndexer.cpp
    ../../native/torrent/TankorentWorkerIndexer.h
    ../../native/torrent/TorrentIndexer.cpp
    ../../native/torrent/PirateBayIndexer.cpp
    ../../native/torrent/ExtTorrentsIndexer.cpp
    ../../native/torrent/TorrentsCsvIndexer.cpp
    ../../native/torrent/KnabenIndexer.cpp)
target_include_directories(vinyl_torrent_support PUBLIC ../../native)
target_link_libraries(vinyl_torrent_support PUBLIC Qt6::Core Qt6::Network)
add_executable(vinyl_live_harness main.cpp
    ../../native/bootstrap/ChildProcessJob.cpp ../../native/bootstrap/ChildProcessJob.h ../../native/vinyl/VinylProvider.h
    ../../native/ProgressStore.h ../../native/CollectionStore.h
    ../../native/player/mpvitem.h ../../native/player/mpvitem.cpp)
target_include_directories(vinyl_live_harness PRIVATE ../../native)
target_link_libraries(vinyl_live_harness PRIVATE Qt6::Quick Qt6::Network MpvQt::MpvQt vinyl_torrent_support)
target_compile_definitions(vinyl_live_harness PRIVATE VINYL_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(vinyl_qml_regression quicktest.cpp)
target_link_libraries(vinyl_qml_regression PRIVATE Qt6::QuickTest Qt6::Quick)
add_executable(music_torrent_matcher_harness ../music_torrent_matcher_harness.cpp)
target_link_libraries(music_torrent_matcher_harness PRIVATE vinyl_torrent_support)
add_executable(vinyl_torrent_provider_contracts torrent_provider_contracts.cpp)
target_link_libraries(vinyl_torrent_provider_contracts PRIVATE vinyl_torrent_support Qt6::Test)
add_executable(vinyl_stream_contracts stream_contracts.cpp)
target_link_libraries(vinyl_stream_contracts PRIVATE vinyl_torrent_support Qt6::Test)
