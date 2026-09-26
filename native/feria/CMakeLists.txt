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
target_link_libraries(feria_discovery PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml)
