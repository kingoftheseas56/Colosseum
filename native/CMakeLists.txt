cmake_minimum_required(VERSION 3.16)
project(colosseum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui)

# --- video player: KDE mpvqt (libmpv-in-QtQuick) + libmpv ---
# Consumes the PROVEN feasibility build (don't rebuild — see kindled-reel recap).
# Override these on the CMake command line to point at vendored copies later.
set(MPVQT_PREFIX  "C:/tools/mpvqt-feasibility/mpvqt-install" CACHE PATH "MpvQt install prefix")
set(LIBMPV_PREFIX "C:/tools/mpvqt-feasibility/libmpv-prefix" CACHE PATH "libmpv dev prefix")
list(PREPEND CMAKE_PREFIX_PATH "${MPVQT_PREFIX}")
# Pre-seed libmpv so MpvQt's bundled FindLibmpv (pkg-config based) succeeds on Windows.
set(Libmpv_INCLUDE_DIRS "${LIBMPV_PREFIX}/include" CACHE PATH "" FORCE)
set(Libmpv_LIBRARIES    "${LIBMPV_PREFIX}/lib/libmpv.dll.a" CACHE FILEPATH "" FORCE)
find_package(MpvQt REQUIRED)

add_executable(colosseum
    main.cpp
    MangaEngine.h
    ProgressStore.h
    engine/WeebCentralScraper.cpp
    engine/WeebCentralScraper.h
    engine/MangaScraper.h
    engine/MangaResult.h
    engine/MangaSeriesDetail.h
    engine/MangaFireCatalogClient.cpp
    engine/MangaFireCatalogClient.h
    engine/MangaDownloader.cpp
    engine/MangaDownloader.h
    player/mpvitem.cpp
    player/mpvitem.h
    player/mpvproperties.h
    player/streamserver.cpp
    player/streamserver.h
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player)
target_link_libraries(colosseum PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui MpvQt::MpvQt)
