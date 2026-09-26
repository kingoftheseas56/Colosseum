cmake_minimum_required(VERSION 3.16)

if(NOT WIN32)
    return()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(feria_host_probe LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
endif()

find_package(unofficial-webview2 CONFIG REQUIRED)

set(FERIA_PROVIDER_HOST_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../native/feria/provider-host")

add_executable(feria_host_probe
    main.cpp
    Main.qml
    "${FERIA_PROVIDER_HOST_DIR}/FeriaProviderHost.cpp"
    "${FERIA_PROVIDER_HOST_DIR}/FeriaProviderHost.h"
    "${FERIA_PROVIDER_HOST_DIR}/FeriaHostItem.cpp"
    "${FERIA_PROVIDER_HOST_DIR}/FeriaHostItem.h")

set_target_properties(feria_host_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

target_include_directories(feria_host_probe PRIVATE
    "${FERIA_PROVIDER_HOST_DIR}")

target_link_libraries(feria_host_probe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    unofficial::webview2::webview2
    ole32
    user32)

target_compile_definitions(feria_host_probe PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN)

add_custom_target(feria_host_probe_qml_copy
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_LIST_DIR}/Main.qml"
            "${CMAKE_CURRENT_BINARY_DIR}/Main.qml"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/Main.qml")
add_dependencies(feria_host_probe feria_host_probe_qml_copy)

add_custom_command(TARGET feria_host_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:unofficial::webview2::webview2>"
            "$<TARGET_FILE_DIR:feria_host_probe>/WebView2Loader.dll")
