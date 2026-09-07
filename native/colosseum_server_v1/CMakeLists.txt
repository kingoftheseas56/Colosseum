cmake_minimum_required(VERSION 3.16)
project(colosseum_server_v1 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)

# In the real Colosseum build this target already exists. A standalone P03
# qualification build must receive a generated fragment extracted from the live
# native/CMakeLists.txt authority. We never carry a second dependency recipe here.
if(NOT TARGET colosseum_libtorrent)
    set(COLOSSEUM_LIBTORRENT_AUTHORITY_FILE "" CACHE FILEPATH
        "Generated CMake fragment extracted from Colosseum's live colosseum_libtorrent authority")
    if(NOT COLOSSEUM_LIBTORRENT_AUTHORITY_FILE OR
       NOT EXISTS "${COLOSSEUM_LIBTORRENT_AUTHORITY_FILE}")
        message(FATAL_ERROR
            "colosseum_libtorrent target is absent. Generate COLOSSEUM_LIBTORRENT_AUTHORITY_FILE from the live Colosseum native/CMakeLists.txt authority before standalone configuration.")
    endif()
    include("${COLOSSEUM_LIBTORRENT_AUTHORITY_FILE}")
endif()

if(NOT TARGET colosseum_libtorrent)
    message(FATAL_ERROR "Generated authority did not define colosseum_libtorrent")
endif()

add_library(colosseum_server_v1 STATIC
    src/Runtime.cpp
)
add_library(server1::runtime ALIAS colosseum_server_v1)

target_include_directories(colosseum_server_v1
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(colosseum_server_v1
    PUBLIC Qt6::Core
    PRIVATE colosseum_libtorrent
)
target_compile_features(colosseum_server_v1 PUBLIC cxx_std_17)

add_executable(colosseum_server_v1_host
    host/main.cpp
)
target_link_libraries(colosseum_server_v1_host PRIVATE server1::runtime Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
