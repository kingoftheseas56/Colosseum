file(GLOB FERIA_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime-facade/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime-facade/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/feed-registry/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/feed-registry/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/app-state/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app-state/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/destination-router/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/destination-router/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/identity-state/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/identity-state/*.h")

add_library(feria_discovery STATIC
    FeriaAccountStore.cpp
    FeriaAccountStore.h
    FeriaBrowserPolicy.h
    FeriaBrowserPolicy.cpp
    PorticoComposition.cpp
    PorticoComposition.h
    PorticoWebBridge.cpp
    PorticoWebBridge.h
    ${FERIA_BACKEND_SOURCES})
target_compile_features(feria_discovery PUBLIC cxx_std_20)
target_include_directories(feria_discovery PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/backend"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime-facade"
    "${CMAKE_CURRENT_SOURCE_DIR}/feed-registry"
    "${CMAKE_CURRENT_SOURCE_DIR}/app-state"
    "${CMAKE_CURRENT_SOURCE_DIR}/destination-router"
    "${CMAKE_CURRENT_SOURCE_DIR}/identity-state")
target_link_libraries(feria_discovery PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::WebEngineQuick Qt6::Sql)

if(WIN32)
    find_package(unofficial-webview2 CONFIG QUIET)
    if(TARGET unofficial::webview2::webview2)
        target_sources(feria_discovery PRIVATE
            provider-host/FeriaProviderHost.cpp provider-host/FeriaProviderHost.h
            provider-host/FeriaHostItem.cpp provider-host/FeriaHostItem.h)
        target_link_libraries(feria_discovery PRIVATE Qt6::Quick
            unofficial::webview2::webview2 ole32 user32)
        target_compile_definitions(feria_discovery PRIVATE FERIA_HAS_WEBVIEW2)
        target_compile_definitions(feria_discovery PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    endif()
endif()
