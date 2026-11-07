add_library(colosseum_nuvio STATIC
    NuvioManifest.h
    NuvioNetwork.cpp NuvioNetwork.h
    NuvioRepositoryStore.cpp NuvioRepositoryStore.h
    NuvioSourceResolver.cpp NuvioSourceResolver.h
    NuvioTmdbCredentials.cpp NuvioTmdbCredentials.h
    NuvioWorkerProtocol.h
    ../engine/TankoyomiNetworkPolicy.cpp)
if(ANDROID)
    target_sources(colosseum_nuvio PRIVATE AndroidProcess.cpp AndroidProcess.h)
    set_target_properties(colosseum_nuvio PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(COLOSSEUM_NUVIO_ANDROID_PROOF)
        target_compile_definitions(colosseum_nuvio PUBLIC COLOSSEUM_NUVIO_ANDROID_PROOF)
    endif()
endif()
target_include_directories(colosseum_nuvio PRIVATE ..)
target_link_libraries(colosseum_nuvio PUBLIC Qt6::Network PRIVATE colosseum_account_credentials)
if(MSVC)
    target_compile_options(colosseum_nuvio PRIVATE /utf-8)
endif()

# Compile only the QuickJS-NG engine. The CLI and its OS/libc bindings are excluded.
add_library(colosseum_nuvio_quickjs STATIC
    vendor/quickjs/dtoa.c
    vendor/quickjs/libregexp.c
    vendor/quickjs/libunicode.c
    vendor/quickjs/quickjs.c)
set_target_properties(colosseum_nuvio_quickjs PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON AUTOMOC OFF AUTORCC OFF)
target_include_directories(colosseum_nuvio_quickjs SYSTEM PUBLIC vendor/quickjs)
if(WIN32)
    target_compile_definitions(colosseum_nuvio_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
else()
    find_package(Threads REQUIRED)
    target_compile_definitions(colosseum_nuvio_quickjs PRIVATE _GNU_SOURCE)
    target_link_libraries(colosseum_nuvio_quickjs PRIVATE Threads::Threads m)
endif()

# Keep the release credential in a single helper-only object, outside the
# compiler cache and compile commands. A keyless build uses the OS vault.
set(nuvio_tmdb_key "$ENV{COLOSSEUM_TMDB_KEY}")
if(NOT nuvio_tmdb_key STREQUAL "")
    if(CMAKE_CURRENT_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        message(FATAL_ERROR "A bundled TMDB key requires a separate CMake build directory.")
    endif()
    string(LENGTH "${nuvio_tmdb_key}" nuvio_tmdb_key_length)
    if(NOT (nuvio_tmdb_key_length EQUAL 32 AND nuvio_tmdb_key MATCHES "^[0-9A-Fa-f]+$")
       AND NOT (nuvio_tmdb_key_length GREATER_EQUAL 64 AND nuvio_tmdb_key_length LESS_EQUAL 4096
                AND nuvio_tmdb_key MATCHES "^[-_A-Za-z0-9]+[.][-_A-Za-z0-9]+[.][-_A-Za-z0-9]+$"))
        message(FATAL_ERROR "COLOSSEUM_TMDB_KEY must be a TMDB v3 key or v4 bearer token.")
    endif()
endif()
set(nuvio_tmdb_source "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NuvioBundledTmdbKey.cpp")
file(CONFIGURE OUTPUT "${nuvio_tmdb_source}" CONTENT [=[
#include "NuvioTmdbCredentials.h"

const char *NuvioTmdb::bundledKey()
{
    return "@nuvio_tmdb_key@";
}
]=] @ONLY)
unset(nuvio_tmdb_key)
add_library(colosseum_nuvio_tmdb_key OBJECT "${nuvio_tmdb_source}")
set_target_properties(colosseum_nuvio_tmdb_key PROPERTIES
    CXX_COMPILER_LAUNCHER "" CXX_CLANG_TIDY "" AUTOMOC OFF AUTORCC OFF)
target_include_directories(colosseum_nuvio_tmdb_key PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(colosseum_nuvio_tmdb_key PRIVATE Qt6::Core)

if(ANDROID)
    # QtService loads this APK library and calls main with a valid Android VM.
    # It reuses the exact QuickJS/network/TMDB implementation used on desktop.
    add_library(colosseum-nuvio-host SHARED WorkerMain.cpp worker_resources.qrc)
    set_target_properties(colosseum-nuvio-host PROPERTIES
        OUTPUT_NAME colosseum_nuvio_host SUFFIX "_${ANDROID_ABI}.so")
    set_target_properties(colosseum_nuvio_quickjs colosseum_nuvio_tmdb_key
        PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(COLOSSEUM_NUVIO_ANDROID_PROOF)
        target_link_libraries(colosseum-nuvio-host PRIVATE log)
    endif()
else()
    add_executable(colosseum-nuvio-host WorkerMain.cpp worker_resources.qrc)
endif()
set_target_properties(colosseum-nuvio-host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(colosseum-nuvio-host PRIVATE ..)
target_link_libraries(colosseum-nuvio-host PRIVATE
    colosseum_nuvio colosseum_nuvio_quickjs colosseum_nuvio_tmdb_key Qt6::WebSockets)
if(MSVC)
    target_compile_options(colosseum-nuvio-host PRIVATE /utf-8)
endif()
