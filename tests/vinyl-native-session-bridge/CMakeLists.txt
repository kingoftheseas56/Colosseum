cmake_minimum_required(VERSION 3.21)
project(VinylNativeSessionBridge LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickTest)
find_package(MpvQt REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../../native/vinyl/backend/CMakeLists.txt")
add_executable(vinyl_native_session_bridge runner.cpp ../../native/vinyl/VinylLocalSource.cpp ../../native/vinyl/VinylLocalSource.h ../../native/vinyl/VinylMusicMetadata.cpp ../../native/engine/VaultIdentity.cpp ../../native/engine/VaultIdentity.h
    ../../native/vinyl/VinylBackendSession.cpp ../../native/vinyl/VinylBackendSession.h
    ../../native/bootstrap/ChildProcessJob.cpp ../../native/bootstrap/ChildProcessJob.h ../../native/player/mpvitem.cpp ../../native/player/mpvitem.h ../../native/player/mpvproperties.h)
target_include_directories(vinyl_native_session_bridge PRIVATE ../../native)
target_link_libraries(vinyl_native_session_bridge PRIVATE colosseum_vinyl_backend
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickTest MpvQt::MpvQt)
target_compile_definitions(vinyl_native_session_bridge PRIVATE
    VINYL_CONTRACT_IMPORTS="${CMAKE_CURRENT_LIST_DIR}/../vinyl-local-session/imports")
if(MSVC)
    target_compile_options(vinyl_native_session_bridge PRIVATE /utf-8)
endif()
