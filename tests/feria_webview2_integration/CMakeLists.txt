cmake_minimum_required(VERSION 3.24)

if(NOT WIN32)
    return()
endif()

project(FeriaWebView2Integration LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test Network Sql WebEngineQuick)
find_package(unofficial-webview2 CONFIG REQUIRED)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(feria_wv2_integration
    runtime.cpp
    "${ROOT}/native/SessionStore.h"
    "${ROOT}/native/feria/FeriaAccountStore.cpp"
    "${ROOT}/native/feria/FeriaAccountStore.h"
    "${ROOT}/native/feria/FeriaBrowserPolicy.cpp"
    "${ROOT}/native/feria/FeriaBrowserPolicy.h"
    "${ROOT}/native/feria/FeriaBrowserOperations.cpp"
    "${ROOT}/native/feria/FeriaBrowserRuntime.cpp"
    "${ROOT}/native/feria/FeriaBrowserRuntime.h"
    "${ROOT}/native/feria/FeriaBrowserOperations.h"
    "${ROOT}/native/feria/app-state/PorticoAppStateBridge.cpp"
    "${ROOT}/native/feria/app-state/PorticoAppStateBridge.h"
    "${ROOT}/native/feria/provider-host/FeriaProviderHost.cpp"
    "${ROOT}/native/feria/provider-host/FeriaProviderHost.h"
    "${ROOT}/native/feria/provider-host/FeriaHostItem.cpp"
    "${ROOT}/native/feria/provider-host/FeriaHostItem.h")
target_include_directories(feria_wv2_integration PRIVATE
    "${ROOT}/native" "${ROOT}/native/feria" "${ROOT}/native/feria/provider-host"
    "${ROOT}/native/feria/app-state")
target_link_libraries(feria_wv2_integration PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest Qt6::Test Qt6::Network Qt6::Sql
    Qt6::WebEngineQuick
    unofficial::webview2::webview2 ole32 user32)
target_compile_definitions(feria_wv2_integration PRIVATE
    FERIA_HAS_WEBVIEW2 FERIA_TEST_CONTROLS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_custom_command(TARGET feria_wv2_integration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:unofficial::webview2::webview2>"
            "$<TARGET_FILE_DIR:feria_wv2_integration>/WebView2Loader.dll")
enable_testing()
add_test(NAME webview2_close_integration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_integration.py
            $<TARGET_FILE:feria_wv2_integration>
            ${CMAKE_CURRENT_SOURCE_DIR}
            --output ${CMAKE_CURRENT_BINARY_DIR}/wv2-results)
