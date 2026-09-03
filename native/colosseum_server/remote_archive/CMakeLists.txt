cmake_minimum_required(VERSION 3.24)
project(colosseum_remote_archive LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

add_library(colosseum_remote_archive STATIC
    LzString.cpp
    RemoteArchive.cpp
    RemoteServices.cpp
    ../../third_party/miniz/miniz.c
)
target_include_directories(colosseum_remote_archive PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/miniz
)
target_link_libraries(colosseum_remote_archive PUBLIC Qt6::Core Qt6::Network)

add_executable(remote_archive_tests
    tests/ProtocolFixtures.cpp
    tests/remote_archive_tests.cpp
)
target_link_libraries(remote_archive_tests PRIVATE colosseum_remote_archive Qt6::Test Qt6::Network)

include(CTest)
add_test(NAME remote_archive_tests COMMAND remote_archive_tests)
