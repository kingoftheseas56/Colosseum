cmake_minimum_required(VERSION 3.22)
project(vinyl_tagged_import_contract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

get_filename_component(COLOSSEUM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MPVQT_PREFIX "" CACHE PATH "MpvQt install prefix")
set(LIBMPV_PREFIX "" CACHE PATH "libmpv development prefix")
if(MPVQT_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${MPVQT_PREFIX}")
endif()
if(WIN32)
    if(NOT LIBMPV_PREFIX)
        message(FATAL_ERROR "Set LIBMPV_PREFIX for the focused Windows Vinyl import test")
    endif()
    if(MSVC)
        set(_vinyl_mpv_implib "mpv.lib")
    else()
        set(_vinyl_mpv_implib "libmpv.dll.a")
    endif()
    set(Libmpv_INCLUDE_DIRS "${LIBMPV_PREFIX}/include" CACHE PATH "libmpv headers" FORCE)
    set(Libmpv_LIBRARIES "${LIBMPV_PREFIX}/lib/${_vinyl_mpv_implib}" CACHE FILEPATH "libmpv link library" FORCE)
endif()
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Test)
find_package(MpvQt REQUIRED)

set(VINYL_TEST_FFMPEG "" CACHE FILEPATH "ffmpeg used only to generate tiny temporary test media")
if(NOT VINYL_TEST_FFMPEG)
    find_program(_vinyl_ffmpeg NAMES ffmpeg ffmpeg.exe)
    set(VINYL_TEST_FFMPEG "${_vinyl_ffmpeg}")
endif()
if(NOT VINYL_TEST_FFMPEG OR NOT EXISTS "${VINYL_TEST_FFMPEG}")
    message(FATAL_ERROR "Set VINYL_TEST_FFMPEG to an ffmpeg executable; the test creates temporary FLAC fixtures")
endif()

add_executable(vinyl_tagged_import_test
    tst_vinyl_tagged_import.cpp
    "${COLOSSEUM_ROOT}/native/vinyl/VinylLocalSource.h"
    "${COLOSSEUM_ROOT}/native/vinyl/VinylLocalSource.cpp"
    "${COLOSSEUM_ROOT}/native/vinyl/VinylMusicMetadata.h"
    "${COLOSSEUM_ROOT}/native/vinyl/VinylMusicMetadata.cpp"
    "${COLOSSEUM_ROOT}/native/engine/VaultIdentity.h"
    "${COLOSSEUM_ROOT}/native/engine/VaultIdentity.cpp"
    "${COLOSSEUM_ROOT}/native/engine/VaultLocation.h"
    "${COLOSSEUM_ROOT}/native/engine/VaultStoreIo.h"
)
target_include_directories(vinyl_tagged_import_test PRIVATE
    "${COLOSSEUM_ROOT}/native"
)
target_link_libraries(vinyl_tagged_import_test PRIVATE
    Qt6::Core
    Qt6::Test
    Libmpv::Libmpv
)
if(MSVC)
    target_compile_options(vinyl_tagged_import_test PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME vinyl_tagged_import_contract COMMAND vinyl_tagged_import_test)
set_tests_properties(vinyl_tagged_import_contract PROPERTIES
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1;VINYL_TEST_FFMPEG=${VINYL_TEST_FFMPEG}"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${LIBMPV_PREFIX}/bin"
    TIMEOUT 60
)
