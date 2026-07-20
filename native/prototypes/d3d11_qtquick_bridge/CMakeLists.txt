cmake_minimum_required(VERSION 3.20)
project(d3d11_qtquick_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Quick Qml)

set(FFMPEG_ROOT "C:/tools/ffmpeg-master-latest-win64-gpl-shared" CACHE PATH
    "Root of the FFmpeg shared development package")
foreach(ffmpeg_library avformat avcodec avutil)
    if(NOT EXISTS "${FFMPEG_ROOT}/lib/${ffmpeg_library}.lib")
        message(FATAL_ERROR "Missing ${FFMPEG_ROOT}/lib/${ffmpeg_library}.lib")
    endif()
endforeach()

qt_add_executable(d3d11_qtquick_bridge
    src/main.cpp
    src/shared_bridge.cpp
    src/shared_bridge.h
    src/frame_producer.cpp
    src/frame_producer.h
    src/ffmpeg_hevc_source.cpp
    src/ffmpeg_hevc_source.h
    src/video_bridge_item.cpp
    src/video_bridge_item.h
    src/slot_ring.h
)

qt_add_resources(d3d11_qtquick_bridge bridge_qml
    PREFIX "/"
    FILES qml/Main.qml
)

target_compile_definitions(d3d11_qtquick_bridge PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(d3d11_qtquick_bridge PRIVATE "${FFMPEG_ROOT}/include")
target_link_libraries(d3d11_qtquick_bridge PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml
    d3d11 dxgi
    "${FFMPEG_ROOT}/lib/avformat.lib"
    "${FFMPEG_ROOT}/lib/avcodec.lib"
    "${FFMPEG_ROOT}/lib/avutil.lib"
)

enable_testing()

add_executable(slot_ring_test
    tests/slot_ring_test.cpp
)
target_include_directories(slot_ring_test PRIVATE src)

add_test(NAME slot_ring_test COMMAND slot_ring_test)
