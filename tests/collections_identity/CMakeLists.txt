cmake_minimum_required(VERSION 3.21)
project(colosseum_collections_identity_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(collections_identity_test
    collections_identity_test.cpp
    ../../native/NamedCollectionsStore.cpp
    ../../native/NamedCollectionsStore.h
    ../../native/engine/ColosseumTitleIdentityRegistry.cpp
    ../../native/engine/ColosseumTitleIdentityRegistry.h)
target_link_libraries(collections_identity_test PRIVATE Qt6::Core)
enable_testing()
add_test(NAME colosseum.collections_identity_test COMMAND collections_identity_test)
set_tests_properties(colosseum.collections_identity_test PROPERTIES LABELS "unit;collections;identity" TIMEOUT 30)

if(MSVC)
    target_compile_options(collections_identity_test PRIVATE /utf-8)
endif()
