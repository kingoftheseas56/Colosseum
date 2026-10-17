cmake_minimum_required(VERSION 3.21)
project(colosseum_reader_probe LANGUAGES CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "This composition probe requires an Android Qt kit")
endif()
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick)
qt_standard_project_setup()
qt_add_executable(reader_probe main.cpp)
target_link_libraries(reader_probe PRIVATE Qt6::Quick)
qt_add_resources(reader_probe probe PREFIX "/" FILES Main.qml)
set_target_properties(reader_probe PROPERTIES
    QT_ANDROID_ABIS "${CMAKE_ANDROID_ARCH_ABI}"
    QT_ANDROID_APP_NAME "Colosseum Reader Composition Probe"
    QT_ANDROID_MIN_SDK_VERSION 28)
