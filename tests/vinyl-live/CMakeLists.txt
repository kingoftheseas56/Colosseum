cmake_minimum_required(VERSION 3.21)
project(vinyl_live_harness LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network QuickTest)
find_package(MpvQt REQUIRED)
add_executable(vinyl_live_harness main.cpp
    ../../native/vinyl/VinylProvider.h
    ../../native/ProgressStore.h ../../native/CollectionStore.h
    ../../native/player/mpvitem.h ../../native/player/mpvitem.cpp)
target_include_directories(vinyl_live_harness PRIVATE ../../native)
target_link_libraries(vinyl_live_harness PRIVATE Qt6::Quick Qt6::Network MpvQt::MpvQt)
target_compile_definitions(vinyl_live_harness PRIVATE VINYL_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(vinyl_qml_regression quicktest.cpp)
target_link_libraries(vinyl_qml_regression PRIVATE Qt6::QuickTest Qt6::Quick)
