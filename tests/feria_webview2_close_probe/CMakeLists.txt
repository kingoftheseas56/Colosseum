cmake_minimum_required(VERSION 3.20)
project(feria_webview2_close_probe LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "This probe requires Windows and the WebView2 runtime")
endif()
find_package(unofficial-webview2 CONFIG REQUIRED)
add_executable(feria_webview2_close_probe main.cpp)
target_compile_features(feria_webview2_close_probe PRIVATE cxx_std_17)
target_compile_definitions(feria_webview2_close_probe PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(feria_webview2_close_probe PRIVATE
    unofficial::webview2::webview2 ole32 user32)
if(MSVC)
    target_compile_options(feria_webview2_close_probe PRIVATE /W4 /EHsc)
endif()
get_target_property(loader_type unofficial::webview2::webview2 TYPE)
if(loader_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET feria_webview2_close_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:unofficial::webview2::webview2>
        $<TARGET_FILE_DIR:feria_webview2_close_probe>)
endif()
