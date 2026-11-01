cmake_minimum_required(VERSION 3.21)
project(ColosseumNamedCollectionsChecks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
get_filename_component(COLOSSEUM_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(named_collections_store_test
    named_collections_store_test.cpp
    "${COLOSSEUM_SOURCE_ROOT}/native/NamedCollectionsStore.cpp"
    "${COLOSSEUM_SOURCE_ROOT}/native/NamedCollectionsStore.h"
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfilePaths.cpp"
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfilePaths.h")
target_include_directories(named_collections_store_test PRIVATE "${COLOSSEUM_SOURCE_ROOT}/native")
target_link_libraries(named_collections_store_test PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(named_collections_store_test PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME colosseum.named_collections_store_test COMMAND named_collections_store_test)
set_tests_properties(colosseum.named_collections_store_test PROPERTIES TIMEOUT 60)

add_executable(profile_collections_preservation_test
    profile_collections_preservation_test.cpp
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfileAdoption.cpp"
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfileAdoption.h"
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfilePaths.cpp"
    "${COLOSSEUM_SOURCE_ROOT}/native/account/ProfilePaths.h")
target_include_directories(profile_collections_preservation_test PRIVATE "${COLOSSEUM_SOURCE_ROOT}/native")
target_link_libraries(profile_collections_preservation_test PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(profile_collections_preservation_test PRIVATE /utf-8)
endif()
add_test(NAME colosseum.profile_collections_preservation_test COMMAND profile_collections_preservation_test)
set_tests_properties(colosseum.profile_collections_preservation_test PROPERTIES TIMEOUT 60)
