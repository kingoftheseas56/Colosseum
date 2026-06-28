cmake_minimum_required(VERSION 3.16)
project(colosseum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui Sql WebEngineQuick WebChannel)

# --- video player: KDE mpvqt (libmpv-in-QtQuick) + libmpv ---
# Consumes the PROVEN feasibility build (don't rebuild — see kindled-reel recap).
# Toolchain-aware: MSVC (Qt WebEngine path, homeward-foundry) consumes the msvc
# MpvQt build + mpv.lib; MinGW the original build + libmpv.dll.a. Same source tree
# builds under either kit. Override MPVQT_PREFIX on the command line to vendor later.
if(MSVC)
    set(_MPVQT_DEFAULT "C:/tools/mpvqt-feasibility/mpvqt-msvc-install")
    set(_LIBMPV_IMPLIB "mpv.lib")
else()
    set(_MPVQT_DEFAULT "C:/tools/mpvqt-feasibility/mpvqt-install")
    set(_LIBMPV_IMPLIB "libmpv.dll.a")
endif()
set(MPVQT_PREFIX  "${_MPVQT_DEFAULT}" CACHE PATH "MpvQt install prefix")
set(LIBMPV_PREFIX "C:/tools/mpvqt-feasibility/libmpv-prefix" CACHE PATH "libmpv dev prefix")
list(PREPEND CMAKE_PREFIX_PATH "${MPVQT_PREFIX}")
# Pre-seed libmpv so MpvQt's bundled FindLibmpv (pkg-config based) succeeds on Windows.
set(Libmpv_INCLUDE_DIRS "${LIBMPV_PREFIX}/include" CACHE PATH "" FORCE)
set(Libmpv_LIBRARIES    "${LIBMPV_PREFIX}/lib/${_LIBMPV_IMPLIB}" CACHE FILEPATH "" FORCE)
find_package(MpvQt REQUIRED)

add_executable(colosseum
    main.cpp
    MangaEngine.h
    ProgressStore.h
    SessionStore.h
    series/seriesindex.cpp
    series/seriesindex.h
    engine/WeebCentralScraper.cpp
    engine/WeebCentralScraper.h
    engine/MangaScraper.h
    engine/MangaResult.h
    engine/MangaSeriesDetail.h
    engine/MangaFireCatalogClient.cpp
    engine/MangaFireCatalogClient.h
    engine/MangaDownloader.cpp
    engine/MangaDownloader.h
    engine/BookDownloader.cpp
    engine/BookDownloader.h
    reader/BookBridge.cpp
    reader/BookBridge.h
    player/mpvitem.cpp
    player/mpvitem.h
    player/mpvproperties.h
    player/streamserver.cpp
    player/streamserver.h
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player series)
target_link_libraries(colosseum PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui Qt6::Sql MpvQt::MpvQt
    Qt6::WebEngineQuick Qt6::WebChannel)

if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    add_custom_command(TARGET colosseum POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:colosseum>/sqldrivers"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_qt_bin_dir}/../plugins/sqldrivers/qsqlite.dll"
            "$<TARGET_FILE_DIR:colosseum>/sqldrivers/qsqlite.dll"
        VERBATIM)
endif()
