add_library(colosseum_nuvio STATIC
    NuvioManifest.h
    NuvioNetwork.cpp NuvioNetwork.h
    NuvioRepositoryStore.cpp NuvioRepositoryStore.h
    NuvioSourceResolver.cpp NuvioSourceResolver.h
    NuvioTmdbCredentials.cpp NuvioTmdbCredentials.h
    NuvioWorkerProtocol.h
    ../engine/TankoyomiNetworkPolicy.cpp)
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

add_executable(colosseum-nuvio-host WorkerMain.cpp worker_resources.qrc)
set_target_properties(colosseum-nuvio-host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(colosseum-nuvio-host PRIVATE ..)
target_link_libraries(colosseum-nuvio-host PRIVATE
    colosseum_nuvio colosseum_nuvio_quickjs Qt6::WebSockets)
if(MSVC)
    target_compile_options(colosseum-nuvio-host PRIVATE /utf-8)
endif()
