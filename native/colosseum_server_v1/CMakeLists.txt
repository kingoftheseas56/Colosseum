cmake_minimum_required(VERSION 3.24)
project(colosseum_server_v1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(P03_DEPENDENCY_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/server1/DEPENDENCY-LOCK.json" CACHE FILEPATH
    "Accepted Server 1.0 dependency lock")
if(NOT EXISTS "${P03_DEPENDENCY_LOCK}")
  message(FATAL_ERROR "P03 required dependency lock is unavailable: ${P03_DEPENDENCY_LOCK}")
endif()

file(READ "${P03_DEPENDENCY_LOCK}" P03_LOCK_JSON)
string(JSON P03_LIBTORRENT_ROOT GET "${P03_LOCK_JSON}" libtorrent package_root)
string(JSON P03_LIBTORRENT_ARCHIVE GET "${P03_LOCK_JSON}" libtorrent archive_identity path)
string(JSON P03_BOOST_ROOT GET "${P03_LOCK_JSON}" boost include_root)
string(JSON P03_OPENSSL_ROOT GET "${P03_LOCK_JSON}" openssl root)
string(JSON P03_QT_GLOBAL_HEADER GET "${P03_LOCK_JSON}" external_files 10 path)
cmake_path(GET P03_QT_GLOBAL_HEADER PARENT_PATH P03_QT_CORE_INCLUDE)
cmake_path(GET P03_QT_CORE_INCLUDE PARENT_PATH P03_QT_INCLUDE)
cmake_path(GET P03_QT_INCLUDE PARENT_PATH P03_QT_ROOT)

set(P03_REQUIRED_FILES
  "${P03_LIBTORRENT_ROOT}/include/libtorrent/version.hpp"
  "${P03_LIBTORRENT_ARCHIVE}"
  "${P03_BOOST_ROOT}/boost/version.hpp"
  "${P03_OPENSSL_ROOT}/include/openssl/opensslv.h"
  "${P03_OPENSSL_ROOT}/lib/libssl.lib"
  "${P03_OPENSSL_ROOT}/lib/libcrypto.lib"
  "${P03_QT_GLOBAL_HEADER}"
)
foreach(P03_REQUIRED_FILE IN LISTS P03_REQUIRED_FILES)
  if(NOT EXISTS "${P03_REQUIRED_FILE}")
    message(FATAL_ERROR "P03 locked dependency unavailable: ${P03_REQUIRED_FILE}")
  endif()
endforeach()

list(PREPEND CMAKE_PREFIX_PATH
  "${P03_QT_ROOT}"
  "${P03_LIBTORRENT_ROOT}"
)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)

add_library(server1_runtime INTERFACE)
target_include_directories(server1_runtime INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(server1_runtime INTERFACE Qt6::Core)

add_executable(server1_host host/main.cpp)
target_link_libraries(server1_host PRIVATE server1_runtime)

include(CTest)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packets/P03.cmake")
server1_register_p03_tests()
